//
// UI-independent tree of the moves of a game: a main line and the variations
// branching from it. Nodes are addressed by a stable integer id (0 is the
// position before the first move), the first child of a node is its main
// continuation and the other children are its variations.
//

#ifndef CHESSGUI_GAMETREE_H
#define CHESSGUI_GAMETREE_H

#include <QString>
#include <QStringList>
#include <QVector>

#include "pgnannotations.h"
#include "rules.h"

class GameTree {
public:
    struct Node {
        QString uci;
        int parent = -1;
        QVector<int> children;
        PlyAnnotations annotations;
        bool alive = true;
    };

    GameTree();

    void clear();

    static constexpr int RootId = 0;

    [[nodiscard]] const Node &node(int id) const;
    [[nodiscard]] Node &node(int id);
    [[nodiscard]] bool isValid(int id) const;

    // Appends `uci` as the last child of `parent` unless the parent already
    // has that move, and returns the id of the node holding it.
    int addChild(int parent, const QString &uci);
    [[nodiscard]] int findChild(int parent, const QString &uci) const;

    // Nodes from the first move down to `id` inclusive (the root is left out).
    [[nodiscard]] QVector<int> pathTo(int id) const;
    // `id` followed by the first-child chain below it.
    [[nodiscard]] QVector<int> lineFrom(int id) const;
    // The main line: the first-child chain from the root.
    [[nodiscard]] QVector<int> mainLine() const;
    [[nodiscard]] QStringList uciOf(const QVector<int> &line) const;
    // Number of plies from the root to `id`.
    [[nodiscard]] int depth(int id) const;

    // Detaches the node and everything below it.
    void removeSubtree(int id);
    // Makes `id` the first child of its parent, and does the same for every
    // ancestor, so that the line through it becomes the main line.
    void promote(int id);

    [[nodiscard]] int variationCount() const;
    // Number of ids ever handed out, dead ones included: ids are 0..count-1.
    [[nodiscard]] int nodeCount() const;

    // Reads the movetext of a PGN game with its variations and comments. Moves
    // are validated from `start`; the comments are decoded the way the
    // controller does for a game without variations.
    [[nodiscard]] static bool parsePgn(const QString &pgn, const Rules &start,
                                       GameTree *out);
    [[nodiscard]] static PlyAnnotations decodeComment(const QString &rawComment);

private:
    QVector<Node> nodes_;
};

#endif // CHESSGUI_GAMETREE_H
