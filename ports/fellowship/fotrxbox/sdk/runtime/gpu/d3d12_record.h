// The Direct3D 12 backend's command list, recorded on several cores (XBE_D3D12_WORKERS, 3 by default; 0: one list
// written directly, as before).
//
// The backend calls cl_->X(...) as on a command list. With workers, each call is appended in order to a compact
// queue of commands (cheap) instead of being recorded; every few thousand commands the queue is cut into a chunk and
// a worker thread replays the chunk into a command list of its own. At Submit all chunks' lists run in their order
// on the queue, so the GPU sees exactly the commands the backend issued, in the same order. A new list inherits no
// state, so each chunk's list first sets everything the backend had set when the chunk began (heaps, root signature,
// pipeline, root arguments, viewports, scissors, blend factor, stencil reference, render targets, topology, vertex
// and index buffers), tracked as the commands are queued.
//
// Render target and depth views are CPU descriptors read when a command is recorded, and the backend rewrites some of
// its view slots within a frame, so each one queued is copied to a slot of a per-frame staging heap first. A command
// that needs the real list (Get(): the upscaler, the UI, an extension) ends the queue for the frame: the queued
// chunks go to the workers and the rest of the frame is recorded directly on the backend's own list.
// Why: The Third Age, job 111: 16,000-18,000 draws a frame, the render thread 15-17 ms a frame in Draw, the game
// waiting 7 ms for it; most of that was the Direct3D 12 calls themselves.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace gpu {

class Recorder {
public:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    static constexpr uint32_t kFrames = 3;
    static constexpr uint32_t kStageRtv = 32768, kStageDsv = 8192;  // staged views a frame
    static constexpr size_t kChunkOps = 6000;                        // commands a chunk

    Recorder() = default;
    Recorder(const Recorder&) = delete;
    ~Recorder() { StopWorkers(); }

    // after the device and the frames' allocators are made; the list starts open on alloc
    void Init(ID3D12Device* dev, ID3D12CommandAllocator* alloc) {
        dev_ = dev;
        alloc_ = alloc;
        dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&direct_));
        const char* e = std::getenv("XBE_D3D12_WORKERS");
        int n = e ? std::atoi(e) : 0;  // off by default: job 115 ran slower (35 fps) with sky flicker
        workers_ = uint32_t(n < 0 ? 0 : n > 8 ? 8 : n);
        if (!workers_) return;
        for (uint32_t f = 0; f < kFrames; f++) {
            FrameLists& fl = frames_[f];
            D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kStageRtv, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
            if (FAILED(dev_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&fl.rtv)))) { workers_ = 0; return; }
            hd = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kStageDsv, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
            if (FAILED(dev_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&fl.dsv)))) { workers_ = 0; return; }
            fl.alloc.resize(workers_);
            fl.lists.resize(workers_);
            fl.used.assign(workers_, 0);
            for (uint32_t w = 0; w < workers_; w++)
                if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&fl.alloc[w])))) {
                    workers_ = 0;
                    return;
                }
        }
        rtv_inc_ = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dsv_inc_ = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        for (uint32_t w = 0; w < workers_; w++) pool_.emplace_back([this, w] { Work(w); });
        queued_ = true;
        direct_open_ = false;
        direct_->Close();  // reopened when needed
    }
    uint32_t Workers() const { return workers_; }

    Recorder* operator->() { return this; }

    // the real list, for code that records on it itself: the rest of this frame (up to Submit) is recorded directly
    ID3D12GraphicsCommandList* Get() {
        if (queued_) GoDirect();
        return direct_.Get();
    }

    // Submit: every command so far, in order, as lists to execute; the caller executes them, then calls Executed
    // (after waiting for the GPU if it resets the frame)
    void Close(std::vector<ID3D12CommandList*>& out, HRESULT& hr) {
        hr = S_OK;
        if (!workers_) {
            hr = direct_->Close();
            out.push_back(direct_.Get());
            return;
        }
        if (queued_) Cut();
        if (direct_open_) {
            hr = direct_->Close();
            direct_open_ = false;
            order_.push_back({nullptr, true});
        }
        Join();
        for (const Slot& s : order_) {
            if (s.direct) {
                out.push_back(direct_.Get());
            } else if (s.chunk && s.chunk->list) {
                if (FAILED(s.chunk->hr)) hr = s.chunk->hr;
                out.push_back(s.chunk->list);
            }
        }
        order_.clear();
        for (Chunk* c : done_chunks_) free_chunks_.push_back(c);
        done_chunks_.clear();
    }
    // the next recording after a Submit: frame's allocator reset (wait) or not; frame index for the new recording
    void Reopen(ID3D12CommandAllocator* alloc, uint32_t frame, bool reset_frame) {
        frame_ = frame % kFrames;
        if (reset_frame) ResetFrameLists();
        if (!workers_) {
            direct_->Reset(alloc, nullptr);
            return;
        }
        alloc_ = alloc;
        queued_ = true;
        direct_open_ = false;
        bind_ = {};
    }
    // the allocators of frame f are free again (its fence has passed)
    void FrameReset(uint32_t f) {
        if (!workers_) return;
        FrameLists& fl = frames_[f % kFrames];
        for (auto& a : fl.alloc) a->Reset();
        for (auto& u : fl.used) u = 0;
        fl.rtv_used = fl.dsv_used = 0;
    }

    // ---- the command list's calls the backend makes ----
    void SetDescriptorHeaps(UINT n, ID3D12DescriptorHeap* const* h) {
        if (!queued_) return direct_->SetDescriptorHeaps(n, h);
        bind_.heaps[0] = n > 0 ? h[0] : nullptr;
        bind_.heaps[1] = n > 1 ? h[1] : nullptr;
        bind_.nheaps = n;
        // tables point into the heaps bound when they were set: after a change they're not restored (the driver
        // reads a table through the bound heap; ImGui's own heap with the game's tables crashed in nvwgf2umx)
        bind_.table_set = 0;
        Op o{kHeaps};
        o.p[0] = bind_.heaps[0];
        o.p[1] = bind_.heaps[1];
        o.u[0] = n;
        Push(o);
    }
    void SetGraphicsRootSignature(ID3D12RootSignature* r) {
        if (!queued_) return direct_->SetGraphicsRootSignature(r);
        bind_.root = r;
        bind_.cbv_set = bind_.table_set = 0;  // a new root signature clears its arguments
        Op o{kRoot};
        o.p[0] = r;
        Push(o);
    }
    void SetGraphicsRootConstantBufferView(UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) {
        if (!queued_) return direct_->SetGraphicsRootConstantBufferView(i, a);
        if (i < kParams) {
            bind_.cbv[i] = a;
            bind_.cbv_set |= 1u << i;
        }
        Op o{kCbv};
        o.u[0] = i;
        o.q = a;
        Push(o);
    }
    void SetGraphicsRootDescriptorTable(UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) {
        if (!queued_) return direct_->SetGraphicsRootDescriptorTable(i, h);
        if (i < kParams) {
            bind_.table[i] = h.ptr;
            bind_.table_set |= 1u << i;
        }
        Op o{kTable};
        o.u[0] = i;
        o.q = h.ptr;
        Push(o);
    }
    void SetPipelineState(ID3D12PipelineState* p) {
        if (!queued_) return direct_->SetPipelineState(p);
        bind_.pso = p;
        Op o{kPso};
        o.p[0] = p;
        Push(o);
    }
    void RSSetViewports(UINT n, const D3D12_VIEWPORT* v) {
        if (!queued_) return direct_->RSSetViewports(n, v);
        n = n > 4 ? 4 : n;
        bind_.nvp = n;
        std::memcpy(bind_.vp, v, sizeof(D3D12_VIEWPORT) * n);
        Op o{kViewports};
        o.u[0] = n;
        o.data = PushBytes(v, sizeof(D3D12_VIEWPORT) * n);
        Push(o);
    }
    void RSSetScissorRects(UINT n, const D3D12_RECT* r) {
        if (!queued_) return direct_->RSSetScissorRects(n, r);
        n = n > 4 ? 4 : n;
        bind_.nsc = n;
        std::memcpy(bind_.sc, r, sizeof(D3D12_RECT) * n);
        Op o{kScissors};
        o.u[0] = n;
        o.data = PushBytes(r, sizeof(D3D12_RECT) * n);
        Push(o);
    }
    void OMSetBlendFactor(const FLOAT f[4]) {
        if (!queued_) return direct_->OMSetBlendFactor(f);
        std::memcpy(bind_.blend, f, sizeof(bind_.blend));
        bind_.blend_set = true;
        Op o{kBlend};
        std::memcpy(o.f, f, sizeof(o.f));
        Push(o);
    }
    void OMSetStencilRef(UINT r) {
        if (!queued_) return direct_->OMSetStencilRef(r);
        bind_.stencil = r;
        bind_.stencil_set = true;
        Op o{kStencil};
        o.u[0] = r;
        Push(o);
    }
    void OMSetRenderTargets(UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE* rt, BOOL single, const D3D12_CPU_DESCRIPTOR_HANDLE* ds) {
        if (!queued_) return direct_->OMSetRenderTargets(n, rt, single, ds);
        // single: one handle for n contiguous views; staged as n separate views either way
        n = n > 8 ? 8 : n;
        bind_.nrt = n;
        for (UINT i = 0; i < n; i++) {
            D3D12_CPU_DESCRIPTOR_HANDLE h = rt[0];
            if (single) h.ptr += size_t(i) * rtv_inc_;
            else h = rt[i];
            bind_.rt[i] = StageRtv(h);
        }
        bind_.has_ds = ds != nullptr;
        if (ds) bind_.ds = StageDsv(*ds);
        bind_.rt_set = true;
        Op o{kTargets};
        o.u[0] = n;
        o.u[1] = bind_.has_ds;
        o.data = PushBytes(bind_.rt, sizeof(D3D12_CPU_DESCRIPTOR_HANDLE) * n);
        o.q = bind_.ds.ptr;
        Push(o);
    }
    void IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY t) {
        if (!queued_) return direct_->IASetPrimitiveTopology(t);
        bind_.topo = t;
        Op o{kTopo};
        o.u[0] = uint32_t(t);
        Push(o);
    }
    void IASetVertexBuffers(UINT start, UINT n, const D3D12_VERTEX_BUFFER_VIEW* v) {
        if (!queued_) return direct_->IASetVertexBuffers(start, n, v);
        if (start == 0 && n <= 16) {
            bind_.nvb = n;
            std::memcpy(bind_.vb, v, sizeof(D3D12_VERTEX_BUFFER_VIEW) * n);
        }
        Op o{kVb};
        o.u[0] = start;
        o.u[1] = n;
        o.data = PushBytes(v, sizeof(D3D12_VERTEX_BUFFER_VIEW) * n);
        Push(o);
    }
    void IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* v) {
        if (!queued_) return direct_->IASetIndexBuffer(v);
        bind_.has_ib = v != nullptr;
        if (v) bind_.ib = *v;
        Op o{kIb};
        o.u[0] = v != nullptr;
        if (v) o.data = PushBytes(v, sizeof(*v));
        Push(o);
    }
    void DrawInstanced(UINT n, UINT inst, UINT start, UINT first_inst) {
        if (!queued_) return direct_->DrawInstanced(n, inst, start, first_inst);
        Op o{kDraw};
        o.u[0] = n;
        o.u[1] = inst;
        o.u[2] = start;
        o.u[3] = first_inst;
        Push(o);
    }
    void DrawIndexedInstanced(UINT n, UINT inst, UINT start, INT base, UINT first_inst) {
        if (!queued_) return direct_->DrawIndexedInstanced(n, inst, start, base, first_inst);
        Op o{kDrawIndexed};
        o.u[0] = n;
        o.u[1] = inst;
        o.u[2] = start;
        o.u[3] = first_inst;
        o.i = base;
        Push(o);
    }
    void ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE h, const FLOAT c[4], UINT n, const D3D12_RECT* r) {
        if (!queued_) return direct_->ClearRenderTargetView(h, c, n, r);
        Op o{kClearRt};
        o.q = StageRtv(h).ptr;
        std::memcpy(o.f, c, sizeof(o.f));
        o.u[0] = n;
        if (n) o.data = PushBytes(r, sizeof(D3D12_RECT) * n);
        Push(o);
    }
    void ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE h, D3D12_CLEAR_FLAGS fl, FLOAT d, UINT8 s, UINT n, const D3D12_RECT* r) {
        if (!queued_) return direct_->ClearDepthStencilView(h, fl, d, s, n, r);
        Op o{kClearDs};
        o.q = StageDsv(h).ptr;
        o.u[0] = n;
        o.u[1] = uint32_t(fl);
        o.u[2] = s;
        o.f[0] = d;
        if (n) o.data = PushBytes(r, sizeof(D3D12_RECT) * n);
        Push(o);
    }
    void ResourceBarrier(UINT n, const D3D12_RESOURCE_BARRIER* b) {
        if (!queued_) return direct_->ResourceBarrier(n, b);
        Op o{kBarrier};
        o.u[0] = n;
        o.data = PushBytes(b, sizeof(D3D12_RESOURCE_BARRIER) * n);
        Push(o);
    }
    void CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* dst, UINT x, UINT y, UINT z,
                           const D3D12_TEXTURE_COPY_LOCATION* src, const D3D12_BOX* box) {
        if (!queued_) return direct_->CopyTextureRegion(dst, x, y, z, src, box);
        Op o{kCopyRegion};
        o.u[0] = x;
        o.u[1] = y;
        o.u[2] = z;
        o.u[3] = box != nullptr;
        o.data = PushBytes(dst, sizeof(*dst));
        PushBytes(src, sizeof(*src));
        if (box) PushBytes(box, sizeof(*box));
        Push(o);
    }
    void CopyResource(ID3D12Resource* dst, ID3D12Resource* src) {
        if (!queued_) return direct_->CopyResource(dst, src);
        Op o{kCopyResource};
        o.p[0] = dst;
        o.p[1] = src;
        Push(o);
    }
    void ResolveSubresource(ID3D12Resource* dst, UINT ds, ID3D12Resource* src, UINT ss, DXGI_FORMAT f) {
        if (!queued_) return direct_->ResolveSubresource(dst, ds, src, ss, f);
        Op o{kResolve};
        o.p[0] = dst;
        o.p[1] = src;
        o.u[0] = ds;
        o.u[1] = ss;
        o.u[2] = uint32_t(f);
        Push(o);
    }
    // occlusion queries can't span two lists: chunks are not cut while one is open
    void BeginQuery(ID3D12QueryHeap* h, D3D12_QUERY_TYPE t, UINT i) {
        if (!queued_) return direct_->BeginQuery(h, t, i);
        query_open_ = true;
        Op o{kBeginQuery};
        o.p[0] = h;
        o.u[0] = uint32_t(t);
        o.u[1] = i;
        Push(o);
    }
    void EndQuery(ID3D12QueryHeap* h, D3D12_QUERY_TYPE t, UINT i) {
        if (!queued_) return direct_->EndQuery(h, t, i);
        query_open_ = false;
        Op o{kEndQuery};
        o.p[0] = h;
        o.u[0] = uint32_t(t);
        o.u[1] = i;
        Push(o);
    }
    void ResolveQueryData(ID3D12QueryHeap* h, D3D12_QUERY_TYPE t, UINT start, UINT n, ID3D12Resource* dst, UINT64 off) {
        if (!queued_) return direct_->ResolveQueryData(h, t, start, n, dst, off);
        Op o{kResolveQuery};
        o.p[0] = h;
        o.p[1] = dst;
        o.u[0] = uint32_t(t);
        o.u[1] = start;
        o.u[2] = n;
        o.q = off;
        Push(o);
    }

    // perf diagnostic: chunks recorded since the last call, and how many frames went direct
    uint64_t TakeChunks() { return chunks_.exchange(0); }

private:
    static constexpr uint32_t kParams = 8;
    enum OpKind : uint8_t {
        kHeaps, kRoot, kCbv, kTable, kPso, kViewports, kScissors, kBlend, kStencil, kTargets, kTopo, kVb, kIb, kDraw,
        kDrawIndexed, kClearRt, kClearDs, kBarrier, kCopyRegion, kCopyResource, kResolve, kBeginQuery, kEndQuery,
        kResolveQuery
    };
    struct Op {
        OpKind kind;
        uint32_t u[4] = {};
        int32_t i = 0;
        float f[4] = {};
        void* p[2] = {};
        uint64_t q = 0;
        uint32_t data = 0;  // offset of extra bytes in the chunk's byte store
    };
    // everything a list must set to continue where the last left off
    struct Bind {
        ID3D12DescriptorHeap* heaps[2] = {};
        uint32_t nheaps = 0;
        ID3D12RootSignature* root = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS cbv[kParams] = {};
        uint64_t table[kParams] = {};
        uint32_t cbv_set = 0, table_set = 0;
        ID3D12PipelineState* pso = nullptr;
        D3D12_VIEWPORT vp[4] = {};
        uint32_t nvp = 0;
        D3D12_RECT sc[4] = {};
        uint32_t nsc = 0;
        float blend[4] = {};
        bool blend_set = false;
        uint32_t stencil = 0;
        bool stencil_set = false;
        D3D12_CPU_DESCRIPTOR_HANDLE rt[8] = {};
        uint32_t nrt = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE ds = {};
        bool has_ds = false, rt_set = false;
        D3D12_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        D3D12_VERTEX_BUFFER_VIEW vb[16] = {};
        uint32_t nvb = 0;
        D3D12_INDEX_BUFFER_VIEW ib = {};
        bool has_ib = false;
    };
    struct Chunk {
        Bind start;
        std::vector<Op> ops;
        std::vector<uint8_t> bytes;
        uint32_t frame = 0;
        ID3D12GraphicsCommandList* list = nullptr;  // set by the worker
        HRESULT hr = S_OK;
        bool done = false;
    };
    struct Slot {
        Chunk* chunk;
        bool direct;
    };
    struct FrameLists {
        std::vector<ComPtr<ID3D12CommandAllocator>> alloc;              // a worker's
        std::vector<std::vector<ComPtr<ID3D12GraphicsCommandList>>> lists;  // a worker's lists
        std::vector<uint32_t> used;                                     // a worker's lists used this frame
        ComPtr<ID3D12DescriptorHeap> rtv, dsv;
        uint32_t rtv_used = 0, dsv_used = 0;
    };

    void Push(const Op& o) {
        if (!cur_) Open();
        cur_->ops.push_back(o);
        if (cur_->ops.size() >= kChunkOps && !query_open_) Cut();
    }
    uint32_t PushBytes(const void* p, size_t n) {
        if (!cur_) Open();
        uint32_t at = uint32_t(cur_->bytes.size());
        cur_->bytes.resize(at + n);
        std::memcpy(cur_->bytes.data() + at, p, n);
        return at;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE StageRtv(D3D12_CPU_DESCRIPTOR_HANDLE h) {
        FrameLists& fl = frames_[frame_];
        if (fl.rtv_used >= kStageRtv) fl.rtv_used = 0;  // (a frame of 32,768 target changes: not seen)
        D3D12_CPU_DESCRIPTOR_HANDLE d = fl.rtv->GetCPUDescriptorHandleForHeapStart();
        d.ptr += size_t(fl.rtv_used++) * rtv_inc_;
        dev_->CopyDescriptorsSimple(1, d, h, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        return d;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE StageDsv(D3D12_CPU_DESCRIPTOR_HANDLE h) {
        FrameLists& fl = frames_[frame_];
        if (fl.dsv_used >= kStageDsv) fl.dsv_used = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE d = fl.dsv->GetCPUDescriptorHandleForHeapStart();
        d.ptr += size_t(fl.dsv_used++) * dsv_inc_;
        dev_->CopyDescriptorsSimple(1, d, h, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        return d;
    }
    void Open() {
        if (free_chunks_.empty()) {
            store_.emplace_back(new Chunk);
            free_chunks_.push_back(store_.back().get());
        }
        cur_ = free_chunks_.back();
        free_chunks_.pop_back();
        cur_->ops.clear();
        cur_->bytes.clear();
        cur_->start = bind_;
        cur_->frame = frame_;
        cur_->list = nullptr;
        cur_->hr = S_OK;
        cur_->done = false;
    }
    // the open chunk to the workers
    void Cut() {
        if (!cur_ || cur_->ops.empty()) return;
        Chunk* c = cur_;
        cur_ = nullptr;
        order_.push_back({c, false});
        done_chunks_.push_back(c);
        {
            std::lock_guard l(m_);
            jobs_.push_back(c);
            pending_++;
        }
        cv_.notify_one();
        chunks_++;
    }
    void Join() {
        std::unique_lock l(m_);
        done_cv_.wait(l, [&] { return pending_ == 0; });
    }
    // from here to Submit, recorded on the backend's own list: queued chunks first, then this list, set as they left
    void GoDirect() {
        Cut();
        direct_->Reset(alloc_, nullptr);
        direct_open_ = true;
        queued_ = false;
        Apply(direct_.Get(), bind_);
    }
    // a fresh list set to b
    static void Apply(ID3D12GraphicsCommandList* l, const Bind& b) {
        if (b.nheaps) l->SetDescriptorHeaps(b.nheaps, const_cast<ID3D12DescriptorHeap**>(b.heaps));
        if (b.root) {
            l->SetGraphicsRootSignature(b.root);
            for (uint32_t i = 0; i < kParams; i++) {
                if (b.cbv_set & (1u << i)) l->SetGraphicsRootConstantBufferView(i, b.cbv[i]);
                if (b.table_set & (1u << i)) l->SetGraphicsRootDescriptorTable(i, D3D12_GPU_DESCRIPTOR_HANDLE{b.table[i]});
            }
        }
        if (b.pso) l->SetPipelineState(b.pso);
        if (b.nvp) l->RSSetViewports(b.nvp, b.vp);
        if (b.nsc) l->RSSetScissorRects(b.nsc, b.sc);
        if (b.blend_set) l->OMSetBlendFactor(b.blend);
        if (b.stencil_set) l->OMSetStencilRef(b.stencil);
        if (b.rt_set) l->OMSetRenderTargets(b.nrt, b.nrt ? b.rt : nullptr, FALSE, b.has_ds ? &b.ds : nullptr);
        if (b.topo != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED) l->IASetPrimitiveTopology(b.topo);
        if (b.nvb) l->IASetVertexBuffers(0, b.nvb, b.vb);
        if (b.has_ib) l->IASetIndexBuffer(&b.ib);
    }
    void Replay(ID3D12GraphicsCommandList* l, const Chunk& c) {
        Apply(l, c.start);
        const uint8_t* B = c.bytes.data();
        for (const Op& o : c.ops) {
            switch (o.kind) {
                case kHeaps: {
                    ID3D12DescriptorHeap* h[2] = {static_cast<ID3D12DescriptorHeap*>(o.p[0]), static_cast<ID3D12DescriptorHeap*>(o.p[1])};
                    l->SetDescriptorHeaps(o.u[0], h);
                    break;
                }
                case kRoot: l->SetGraphicsRootSignature(static_cast<ID3D12RootSignature*>(o.p[0])); break;
                case kCbv: l->SetGraphicsRootConstantBufferView(o.u[0], o.q); break;
                case kTable: l->SetGraphicsRootDescriptorTable(o.u[0], D3D12_GPU_DESCRIPTOR_HANDLE{o.q}); break;
                case kPso: l->SetPipelineState(static_cast<ID3D12PipelineState*>(o.p[0])); break;
                case kViewports: l->RSSetViewports(o.u[0], reinterpret_cast<const D3D12_VIEWPORT*>(B + o.data)); break;
                case kScissors: l->RSSetScissorRects(o.u[0], reinterpret_cast<const D3D12_RECT*>(B + o.data)); break;
                case kBlend: l->OMSetBlendFactor(o.f); break;
                case kStencil: l->OMSetStencilRef(o.u[0]); break;
                case kTargets: {
                    D3D12_CPU_DESCRIPTOR_HANDLE ds{size_t(o.q)};
                    l->OMSetRenderTargets(o.u[0], o.u[0] ? reinterpret_cast<const D3D12_CPU_DESCRIPTOR_HANDLE*>(B + o.data) : nullptr,
                                          FALSE, o.u[1] ? &ds : nullptr);
                    break;
                }
                case kTopo: l->IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY(o.u[0])); break;
                case kVb: l->IASetVertexBuffers(o.u[0], o.u[1], reinterpret_cast<const D3D12_VERTEX_BUFFER_VIEW*>(B + o.data)); break;
                case kIb: l->IASetIndexBuffer(o.u[0] ? reinterpret_cast<const D3D12_INDEX_BUFFER_VIEW*>(B + o.data) : nullptr); break;
                case kDraw: l->DrawInstanced(o.u[0], o.u[1], o.u[2], o.u[3]); break;
                case kDrawIndexed: l->DrawIndexedInstanced(o.u[0], o.u[1], o.u[2], o.i, o.u[3]); break;
                case kClearRt:
                    l->ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE{size_t(o.q)}, o.f, o.u[0],
                                             o.u[0] ? reinterpret_cast<const D3D12_RECT*>(B + o.data) : nullptr);
                    break;
                case kClearDs:
                    l->ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE{size_t(o.q)}, D3D12_CLEAR_FLAGS(o.u[1]), o.f[0],
                                             UINT8(o.u[2]), o.u[0], o.u[0] ? reinterpret_cast<const D3D12_RECT*>(B + o.data) : nullptr);
                    break;
                case kBarrier: l->ResourceBarrier(o.u[0], reinterpret_cast<const D3D12_RESOURCE_BARRIER*>(B + o.data)); break;
                case kCopyRegion: {
                    auto dst = reinterpret_cast<const D3D12_TEXTURE_COPY_LOCATION*>(B + o.data);
                    auto src = dst + 1;
                    auto box = o.u[3] ? reinterpret_cast<const D3D12_BOX*>(src + 1) : nullptr;
                    l->CopyTextureRegion(dst, o.u[0], o.u[1], o.u[2], src, box);
                    break;
                }
                case kCopyResource: l->CopyResource(static_cast<ID3D12Resource*>(o.p[0]), static_cast<ID3D12Resource*>(o.p[1])); break;
                case kResolve:
                    l->ResolveSubresource(static_cast<ID3D12Resource*>(o.p[0]), o.u[0], static_cast<ID3D12Resource*>(o.p[1]),
                                          o.u[1], DXGI_FORMAT(o.u[2]));
                    break;
                case kBeginQuery: l->BeginQuery(static_cast<ID3D12QueryHeap*>(o.p[0]), D3D12_QUERY_TYPE(o.u[0]), o.u[1]); break;
                case kEndQuery: l->EndQuery(static_cast<ID3D12QueryHeap*>(o.p[0]), D3D12_QUERY_TYPE(o.u[0]), o.u[1]); break;
                case kResolveQuery:
                    l->ResolveQueryData(static_cast<ID3D12QueryHeap*>(o.p[0]), D3D12_QUERY_TYPE(o.u[0]), o.u[1], o.u[2],
                                        static_cast<ID3D12Resource*>(o.p[1]), o.q);
                    break;
            }
        }
    }
    void Work(uint32_t w) {
        for (;;) {
            Chunk* c;
            {
                std::unique_lock l(m_);
                cv_.wait(l, [&] { return stop_ || !jobs_.empty(); });
                if (stop_ && jobs_.empty()) return;
                c = jobs_.front();
                jobs_.pop_front();
            }
            FrameLists& fl = frames_[c->frame];
            auto& lists = fl.lists[w];
            uint32_t& used = fl.used[w];
            if (used == lists.size()) {
                ComPtr<ID3D12GraphicsCommandList> nl;
                if (SUCCEEDED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, fl.alloc[w].Get(), nullptr,
                                                      IID_PPV_ARGS(&nl))))
                    nl->Close();
                lists.push_back(nl);
            }
            ID3D12GraphicsCommandList* l = lists[used++].Get();
            if (l && SUCCEEDED(l->Reset(fl.alloc[w].Get(), nullptr))) {
                Replay(l, *c);
                c->hr = l->Close();
                c->list = l;
            } else {
                c->hr = E_FAIL;
            }
            {
                std::lock_guard g(m_);
                c->done = true;
                pending_--;
            }
            done_cv_.notify_all();
        }
    }
    void ResetFrameLists() {}
    void StopWorkers() {
        {
            std::lock_guard l(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : pool_)
            if (t.joinable()) t.join();
        pool_.clear();
    }

    ID3D12Device* dev_ = nullptr;
    ComPtr<ID3D12GraphicsCommandList> direct_;
    ID3D12CommandAllocator* alloc_ = nullptr;  // the backend's allocator for the frame being recorded
    bool direct_open_ = true, queued_ = false, query_open_ = false;
    uint32_t workers_ = 0, frame_ = 0, rtv_inc_ = 0, dsv_inc_ = 0;
    Bind bind_;
    Chunk* cur_ = nullptr;
    std::vector<std::unique_ptr<Chunk>> store_;
    std::vector<Chunk*> free_chunks_, done_chunks_;
    std::vector<Slot> order_;
    FrameLists frames_[kFrames];
    std::vector<std::thread> pool_;
    std::mutex m_;
    std::condition_variable cv_, done_cv_;
    std::deque<Chunk*> jobs_;
    uint32_t pending_ = 0;
    bool stop_ = false;
    std::atomic<uint64_t> chunks_{0};
};

}  // namespace gpu
