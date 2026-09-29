//
// Exact result of king and pawn against king.
//

#ifndef CHESSGUI_KPK_BITBASE_H
#define CHESSGUI_KPK_BITBASE_H

#include "rules.h"

// Solves every position of king and pawn against king once, by retrograde
// analysis, and answers from the table afterwards. The table is generated on
// first use (thread-safe) in a few milliseconds; the algorithm is written from
// the rules of the game and does not derive from any other engine's tablebase
// generator.
namespace KpkBitbase {

// True when the side with the pawn wins, false when the position is drawn (the
// pawn is lost, or the defender holds the opposition or a corner). The position
// has to be legal: the three pieces on distinct squares, the kings not adjacent
// and the side that just moved not in check. Squares follow `Rules::Position`
// (row 0 is rank 8).
[[nodiscard]] bool isWin(Rules::Color pawnColor, Rules::Color sideToMove,
                         Rules::Position pawn, Rules::Position pawnKing,
                         Rules::Position bareKing);

} // namespace KpkBitbase

#endif // CHESSGUI_KPK_BITBASE_H
