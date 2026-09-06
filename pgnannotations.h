//
// PGN visual annotations: [%csl ...] (colored squares) and [%cal ...] (colored arrows).
//

#ifndef CHESSGUI_PGNANNOTATIONS_H
#define CHESSGUI_PGNANNOTATIONS_H

#include <QColor>
#include <QString>
#include <optional>
#include <vector>

#include "rules.h"

struct UserArrow {
    Rules::Position from;
    Rules::Position to;
    QColor color;

    friend bool operator==(const UserArrow &lhs, const UserArrow &rhs) = default;
};

struct SquareAnnotation {
    Rules::Position position;
    QColor color;

    friend bool operator==(const SquareAnnotation &lhs, const SquareAnnotation &rhs) = default;
};

enum class AuditSeverity {
    None,
    Inaccuracy,
    Mistake,
    Blunder
};

struct AuditAnnotation {
    AuditSeverity severity = AuditSeverity::None;
    int centipawnLoss = 0;
    QString bestMove;
    QString playedMove;
    bool forcedMate = false;

    friend bool operator==(const AuditAnnotation &, const AuditAnnotation &) = default;
    [[nodiscard]] bool isValid() const { return severity != AuditSeverity::None; }
};

struct PlyAnnotations {
    // User-owned data is intentionally separate from engine audit data.
    std::vector<UserArrow> arrows;
    std::vector<SquareAnnotation> squares;
    QString comment;
    AuditAnnotation audit;

    friend bool operator==(const PlyAnnotations &lhs, const PlyAnnotations &rhs) = default;

    [[nodiscard]] bool empty() const {
        return arrows.empty() && squares.empty() && comment.isEmpty() && !audit.isValid();
    }
};

namespace PgnAnnotations {

// Standard annotation colors
[[nodiscard]] QColor greenColor();
[[nodiscard]] QColor redColor();
[[nodiscard]] QColor blueColor();
[[nodiscard]] QColor orangeColor();

// Convert character code ('G', 'R', 'B', 'Y') to QColor
[[nodiscard]] QColor codeToColor(char code);

// Convert QColor to standard code ('G', 'R', 'B', 'Y')
[[nodiscard]] char colorToCode(const QColor &color);

// Position <-> algebraic square, e.g. {7, 4} -> "e1"
[[nodiscard]] QString positionToSquare(Rules::Position pos);
[[nodiscard]] std::optional<Rules::Position> squareToPosition(const QString &sq);

// Encode arrows and squares into PGN comment format, e.g. "[%csl Gc4,Re5] [%cal Gc4e5]"
[[nodiscard]] QString encode(const std::vector<UserArrow> &arrows,
                             const std::vector<SquareAnnotation> &squares);

// Parse comment text containing [%csl ...] and [%cal ...] tags into annotations
bool decode(const QString &comment,
            std::vector<UserArrow> &outArrows,
            std::vector<SquareAnnotation> &outSquares);

// Strips [%csl ...] and [%cal ...] tags from comment, returning remaining text commentary.
[[nodiscard]] QString stripAnnotationTags(const QString &comment);

// Formats visual annotations and optional comment text into comment body (without outer braces)
[[nodiscard]] QString formatComment(const std::vector<UserArrow> &arrows,
                                    const std::vector<SquareAnnotation> &squares,
                                    const QString &commentText = QString());
[[nodiscard]] QString auditSeverityText(AuditSeverity severity);
[[nodiscard]] QString auditSymbol(AuditSeverity severity);
[[nodiscard]] int auditNag(AuditSeverity severity);
[[nodiscard]] QColor auditColor(AuditSeverity severity);
[[nodiscard]] QString formatAuditComment(const AuditAnnotation &audit);
[[nodiscard]] std::optional<AuditAnnotation> decodeAudit(const QString &comment);
[[nodiscard]] QString stripAuditTags(const QString &comment);

} // namespace PgnAnnotations

#endif // CHESSGUI_PGNANNOTATIONS_H
