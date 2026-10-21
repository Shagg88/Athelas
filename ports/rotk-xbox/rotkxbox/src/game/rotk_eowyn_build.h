#pragma once
// Éowyn in Faramir's place (rotk_eowyn.cpp): her pieces from the player's own Pelennor Fields (pel02.scx) put in
// Faramir's files, as they are read. No game data here.
#include <cstdint>
#include <string>
#include <vector>

namespace rotk::eowyn {

// her pieces: the textures, the mesh and the rig (for its bones) of the player in the level's scenes
struct Pieces {
    std::vector<uint8_t> textures, mesh, rig;
};
bool FindPieces(const std::vector<uint8_t>& pel02, Pieces& out, std::string& why);

// a hero file of Faramir's (Char_Far, Foll_Far) with her mesh and textures: his rig, moves and weapons stay his. Her
// mesh is bound to his skeleton (each of her bones to his of the same name, else the nearest by where it is), with his
// bow and arrow. His voice (his events' channel 2) is made quiet
bool BuildHero(const std::vector<uint8_t>& faramir, const Pieces& her, std::vector<uint8_t>& out, std::string& why);
// the menus' file (Fro03): Faramir's figure on the character select dressed as her, in place
bool BuildMenu(std::vector<uint8_t>& fro03, const Pieces& her, std::string& why);
// the game's shared file (GlobScen): Faramir's face on the HUD as hers (made from her face texture), in place
bool BuildPortrait(std::vector<uint8_t>& globscen, const Pieces& her, std::string& why);

}  // namespace rotk::eowyn
