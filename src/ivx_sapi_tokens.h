#pragma once

// Publishing the voices to SAPI5.
//
// Each voice becomes a token under
//   ...\Microsoft\Speech\Voices\Tokens\Infovox230_<name>
// carrying the engine's class id and the voice's attributes.
//
// An earlier version published them through a token enumerator instead, which
// is dynamic and needs no per-voice keys. Two things ruled it out: SAPI only
// looks for enumerators under HKEY_LOCAL_MACHINE, so a user without
// administrator rights got no voices at all; and applications that read the
// Tokens key directly rather than going through SAPI's category API cannot see
// enumerator-published voices. Tokens are what every SAPI host understands.
//
// The cost is that the list is a snapshot: after editing voices.ini, run
// `Infovox230Diag register` (or the Refresh shortcut the installer creates) to
// republish. That is the only place any of this touches the registry -- the
// Infovox engine itself still reads none of it.

#include <windows.h>

#include "ivx_catalog.h"

namespace ivx {
namespace sapi5 {

// The catalogue this dll publishes and speaks from, loaded once.
const Catalog& shared_catalog();

// Writes a token per voice under `root` (HKEY_LOCAL_MACHINE for every user,
// HKEY_CURRENT_USER for just this one). Returns false if the keys could not be
// written, which is how the caller knows to try the other root.
bool register_voices(HKEY root);

// Removes every token this product wrote, leaving other engines' alone.
void unregister_voices(HKEY root);

}  // namespace sapi5
}  // namespace ivx
