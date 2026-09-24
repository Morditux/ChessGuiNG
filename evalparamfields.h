//
// Description of the tunable HeuristicEval weights, shared by the
// configuration file and the settings dialog so that neither can drift from
// the EvalParams struct itself.
//

#ifndef CHESSGUI_EVAL_PARAM_FIELDS_H
#define CHESSGUI_EVAL_PARAM_FIELDS_H

#include <QCoreApplication>
#include <QString>
#include <vector>

#include "heuristiceval.h"

namespace EvalParamFields {

// One weight: a stable key in chessGui.conf, an English label and section for
// the dialog, the accepted range and the member it stores into.
struct Field {
    const char *key;
    const char *label;
    const char *group;
    int minimum;
    int maximum;
    int HeuristicEval::EvalParams::*member;
};

// The label and the group are wrapped in QT_TRANSLATE_NOOP at the call site,
// where lupdate can see the literals; the dialog looks them up with
// QCoreApplication::translate("EvalParams", ...).
#define CHESSGUI_EVAL_FIELD(memberName, label, group, minimum, maximum)        \
    {                                                                          \
        #memberName, label, group, minimum, maximum,                           \
            &HeuristicEval::EvalParams::memberName                             \
    }

inline const std::vector<Field> &all() {
    static const std::vector<Field> fields = {
        CHESSGUI_EVAL_FIELD(
            pawnValue, QT_TRANSLATE_NOOP("EvalParams", "Pawn value"),
            QT_TRANSLATE_NOOP("EvalParams", "Material"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            knightValue, QT_TRANSLATE_NOOP("EvalParams", "Knight value"),
            QT_TRANSLATE_NOOP("EvalParams", "Material"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            bishopValue, QT_TRANSLATE_NOOP("EvalParams", "Bishop value"),
            QT_TRANSLATE_NOOP("EvalParams", "Material"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            rookValue, QT_TRANSLATE_NOOP("EvalParams", "Rook value"),
            QT_TRANSLATE_NOOP("EvalParams", "Material"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            queenValue, QT_TRANSLATE_NOOP("EvalParams", "Queen value"),
            QT_TRANSLATE_NOOP("EvalParams", "Material"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            tempoBonus, QT_TRANSLATE_NOOP("EvalParams", "Tempo bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Side to move"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            inCheckPenalty, QT_TRANSLATE_NOOP("EvalParams", "In-check penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Side to move"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            doubledPawnPenaltyMG, QT_TRANSLATE_NOOP("EvalParams", "Doubled pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            doubledPawnPenaltyEG, QT_TRANSLATE_NOOP("EvalParams", "Doubled pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            isolatedPawnPenaltyMG, QT_TRANSLATE_NOOP("EvalParams", "Isolated pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            isolatedPawnPenaltyEG, QT_TRANSLATE_NOOP("EvalParams", "Isolated pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            supportedPawnBonusMG, QT_TRANSLATE_NOOP("EvalParams", "Supported pawn bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            supportedPawnBonusEG, QT_TRANSLATE_NOOP("EvalParams", "Supported pawn bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            backwardPawnPenaltyMG, QT_TRANSLATE_NOOP("EvalParams", "Backward pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            backwardPawnPenaltyEG, QT_TRANSLATE_NOOP("EvalParams", "Backward pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            candidatePawnBonusMG, QT_TRANSLATE_NOOP("EvalParams", "Candidate pawn bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            candidatePawnBonusEG, QT_TRANSLATE_NOOP("EvalParams", "Candidate pawn bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            candidatePawnRankBonusMG, QT_TRANSLATE_NOOP("EvalParams", "Candidate pawn rank bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            candidatePawnRankBonusEG, QT_TRANSLATE_NOOP("EvalParams", "Candidate pawn rank bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnBaseMG, QT_TRANSLATE_NOOP("EvalParams", "Passed pawn base bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnBaseEG, QT_TRANSLATE_NOOP("EvalParams", "Passed pawn base bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnRankBonusMG, QT_TRANSLATE_NOOP("EvalParams", "Passed pawn rank bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnRankBonusEG, QT_TRANSLATE_NOOP("EvalParams", "Passed pawn rank bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnBlockedPenaltyMG, QT_TRANSLATE_NOOP("EvalParams", "Blocked passed pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnBlockedPenaltyEG, QT_TRANSLATE_NOOP("EvalParams", "Blocked passed pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnControlledPenaltyMG, QT_TRANSLATE_NOOP("EvalParams", "Controlled passed pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnControlledPenaltyEG, QT_TRANSLATE_NOOP("EvalParams", "Controlled passed pawn penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnRookBehindMG, QT_TRANSLATE_NOOP("EvalParams", "Rook behind the passed pawn"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnRookBehindEG, QT_TRANSLATE_NOOP("EvalParams", "Rook behind the passed pawn"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnEnemyRookBehindMG, QT_TRANSLATE_NOOP("EvalParams", "Enemy rook behind the passed pawn"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            passedPawnEnemyRookBehindEG, QT_TRANSLATE_NOOP("EvalParams", "Enemy rook behind the passed pawn"),
            QT_TRANSLATE_NOOP("EvalParams", "Pawn structure"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            mobilityPawnMG, QT_TRANSLATE_NOOP("EvalParams", "Pawn mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityPawnEG, QT_TRANSLATE_NOOP("EvalParams", "Pawn mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityKnightMG, QT_TRANSLATE_NOOP("EvalParams", "Knight mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityKnightEG, QT_TRANSLATE_NOOP("EvalParams", "Knight mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityBishopMG, QT_TRANSLATE_NOOP("EvalParams", "Bishop mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityBishopEG, QT_TRANSLATE_NOOP("EvalParams", "Bishop mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityRookMG, QT_TRANSLATE_NOOP("EvalParams", "Rook mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityRookEG, QT_TRANSLATE_NOOP("EvalParams", "Rook mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityQueenMG, QT_TRANSLATE_NOOP("EvalParams", "Queen mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityQueenEG, QT_TRANSLATE_NOOP("EvalParams", "Queen mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityKingMG, QT_TRANSLATE_NOOP("EvalParams", "King mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            mobilityKingEG, QT_TRANSLATE_NOOP("EvalParams", "King mobility"),
            QT_TRANSLATE_NOOP("EvalParams", "Mobility"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            spaceBonusMG, QT_TRANSLATE_NOOP("EvalParams", "Central space bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Space"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            spaceBonusEG, QT_TRANSLATE_NOOP("EvalParams", "Central space bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Space"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            castledBonus, QT_TRANSLATE_NOOP("EvalParams", "Castled king bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            castlingRightsBonus, QT_TRANSLATE_NOOP("EvalParams", "Castling rights bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingAttackPenalty, QT_TRANSLATE_NOOP("EvalParams", "Attacked squares near the king"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingAttackPawnPercent, QT_TRANSLATE_NOOP("EvalParams", "King attack by a pawn (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingAttackKnightPercent, QT_TRANSLATE_NOOP("EvalParams", "King attack by a knight (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingAttackBishopPercent, QT_TRANSLATE_NOOP("EvalParams", "King attack by a bishop (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingAttackRookPercent, QT_TRANSLATE_NOOP("EvalParams", "King attack by a rook (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingAttackQueenPercent, QT_TRANSLATE_NOOP("EvalParams", "King attack by a queen (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingShelterBonus, QT_TRANSLATE_NOOP("EvalParams", "Pawn shelter bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingShelterAdjacentFilePercent, QT_TRANSLATE_NOOP("EvalParams", "Shelter on an adjacent file (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            kingShelterSecondRankPercent, QT_TRANSLATE_NOOP("EvalParams", "Shelter on the second rank (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            kingShelterThirdRankPercent, QT_TRANSLATE_NOOP("EvalParams", "Shelter on the third rank (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            kingShelterEndgamePercent, QT_TRANSLATE_NOOP("EvalParams", "Shelter kept in the endgame (%)"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            100),
        CHESSGUI_EVAL_FIELD(
            openFileNearKingPenalty, QT_TRANSLATE_NOOP("EvalParams", "Open file near the king penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            kingPawnStormPenalty, QT_TRANSLATE_NOOP("EvalParams", "Enemy pawn storm penalty"),
            QT_TRANSLATE_NOOP("EvalParams", "King safety"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            rookOpenFileMG, QT_TRANSLATE_NOOP("EvalParams", "Rook on an open file"),
            QT_TRANSLATE_NOOP("EvalParams", "Rooks"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            rookOpenFileEG, QT_TRANSLATE_NOOP("EvalParams", "Rook on an open file"),
            QT_TRANSLATE_NOOP("EvalParams", "Rooks"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            rookSemiOpenFileMG, QT_TRANSLATE_NOOP("EvalParams", "Rook on a semi-open file"),
            QT_TRANSLATE_NOOP("EvalParams", "Rooks"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            rookSemiOpenFileEG, QT_TRANSLATE_NOOP("EvalParams", "Rook on a semi-open file"),
            QT_TRANSLATE_NOOP("EvalParams", "Rooks"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            rookSeventhRankMG, QT_TRANSLATE_NOOP("EvalParams", "Rook on the seventh rank"),
            QT_TRANSLATE_NOOP("EvalParams", "Rooks"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            rookSeventhRankEG, QT_TRANSLATE_NOOP("EvalParams", "Rook on the seventh rank"),
            QT_TRANSLATE_NOOP("EvalParams", "Rooks"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            mopUpMaterialThreshold, QT_TRANSLATE_NOOP("EvalParams", "Material advantage that starts the mop-up"),
            QT_TRANSLATE_NOOP("EvalParams", "Endgame mop-up"), 0,
            2000),
        CHESSGUI_EVAL_FIELD(
            mopUpEdgeBonus, QT_TRANSLATE_NOOP("EvalParams", "Enemy king on the edge bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Endgame mop-up"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            mopUpKingProximityBonus, QT_TRANSLATE_NOOP("EvalParams", "King proximity bonus"),
            QT_TRANSLATE_NOOP("EvalParams", "Endgame mop-up"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            hangingPieceDivisor, QT_TRANSLATE_NOOP("EvalParams", "Hanging piece divisor"),
            QT_TRANSLATE_NOOP("EvalParams", "Threats"), 1,
            100),
        CHESSGUI_EVAL_FIELD(
            cheapAttackerMargin, QT_TRANSLATE_NOOP("EvalParams", "Cheap attacker margin"),
            QT_TRANSLATE_NOOP("EvalParams", "Threats"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            cheapAttackerDivisor, QT_TRANSLATE_NOOP("EvalParams", "Cheap attacker divisor"),
            QT_TRANSLATE_NOOP("EvalParams", "Threats"), 1,
            100),
        CHESSGUI_EVAL_FIELD(
            bishopPairMG, QT_TRANSLATE_NOOP("EvalParams", "Bishop pair"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            bishopPairEG, QT_TRANSLATE_NOOP("EvalParams", "Bishop pair"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            outpostKnightMG, QT_TRANSLATE_NOOP("EvalParams", "Knight outpost"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            outpostKnightEG, QT_TRANSLATE_NOOP("EvalParams", "Knight outpost"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            outpostBishopMG, QT_TRANSLATE_NOOP("EvalParams", "Bishop outpost"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            outpostBishopEG, QT_TRANSLATE_NOOP("EvalParams", "Bishop outpost"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            badBishopPenalty, QT_TRANSLATE_NOOP("EvalParams", "Bad bishop penalty per pawn"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            badBishopCap, QT_TRANSLATE_NOOP("EvalParams", "Bad bishop penalty cap"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            connectedRooksMG, QT_TRANSLATE_NOOP("EvalParams", "Connected rooks"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
        CHESSGUI_EVAL_FIELD(
            connectedRooksEG, QT_TRANSLATE_NOOP("EvalParams", "Connected rooks"),
            QT_TRANSLATE_NOOP("EvalParams", "Piece specific"), 0,
            1000),
    };
    return fields;
}

#undef CHESSGUI_EVAL_FIELD

} // namespace EvalParamFields

#endif // CHESSGUI_EVAL_PARAM_FIELDS_H
