#include "gametree.h"

#include <QHash>

#include <utility>
#include <vector>

GameTree::GameTree() {
    clear();
}

void GameTree::clear() {
    nodes_.clear();
    nodes_.append(Node{});
}

const GameTree::Node &GameTree::node(int id) const {
    return nodes_.at(id);
}

GameTree::Node &GameTree::node(int id) {
    return nodes_[id];
}

bool GameTree::isValid(int id) const {
    return id >= 0 && id < nodes_.size() && nodes_.at(id).alive;
}

int GameTree::findChild(int parent, const QString &uci) const {
    if (!isValid(parent)) {
        return -1;
    }
    for (const int child : nodes_.at(parent).children) {
        if (nodes_.at(child).uci == uci) {
            return child;
        }
    }
    return -1;
}

int GameTree::addChild(int parent, const QString &uci) {
    if (const int existing = findChild(parent, uci); existing >= 0) {
        return existing;
    }
    Node child;
    child.uci = uci;
    child.parent = parent;
    nodes_.append(child);
    const int id = nodes_.size() - 1;
    nodes_[parent].children.append(id);
    return id;
}

QVector<int> GameTree::pathTo(int id) const {
    QVector<int> path;
    while (isValid(id) && id != RootId) {
        path.prepend(id);
        id = nodes_.at(id).parent;
    }
    return path;
}

QVector<int> GameTree::lineFrom(int id) const {
    QVector<int> line;
    while (isValid(id)) {
        line.append(id);
        const auto &children = nodes_.at(id).children;
        if (children.isEmpty()) {
            break;
        }
        id = children.first();
    }
    return line;
}

QVector<int> GameTree::mainLine() const {
    QVector<int> line;
    int id = RootId;
    while (!nodes_.at(id).children.isEmpty()) {
        id = nodes_.at(id).children.first();
        line.append(id);
    }
    return line;
}

QStringList GameTree::uciOf(const QVector<int> &line) const {
    QStringList moves;
    moves.reserve(line.size());
    for (const int id : line) {
        moves.append(nodes_.at(id).uci);
    }
    return moves;
}

int GameTree::depth(int id) const {
    return pathTo(id).size();
}

void GameTree::removeSubtree(int id) {
    if (!isValid(id) || id == RootId) {
        return;
    }
    const int parent = nodes_.at(id).parent;
    if (isValid(parent)) {
        nodes_[parent].children.removeAll(id);
    }
    std::vector<int> pending{id};
    while (!pending.empty()) {
        const int current = pending.back();
        pending.pop_back();
        nodes_[current].alive = false;
        for (const int child : std::as_const(nodes_[current].children)) {
            pending.push_back(child);
        }
        nodes_[current].children.clear();
    }
}

void GameTree::promote(int id) {
    while (isValid(id) && id != RootId) {
        const int parent = nodes_.at(id).parent;
        auto &siblings = nodes_[parent].children;
        const int index = siblings.indexOf(id);
        if (index > 0) {
            siblings.remove(index);
            siblings.prepend(id);
        }
        id = parent;
    }
}

int GameTree::nodeCount() const {
    return nodes_.size();
}

int GameTree::variationCount() const {
    int count = 0;
    for (const Node &n : nodes_) {
        if (n.alive && n.children.size() > 1) {
            count += n.children.size() - 1;
        }
    }
    return count;
}

PlyAnnotations GameTree::decodeComment(const QString &rawComment) {
    PlyAnnotations annotations;
    if (rawComment.isEmpty()) {
        return annotations;
    }
    PgnAnnotations::decode(rawComment, annotations.arrows, annotations.squares);
    if (const auto audit = PgnAnnotations::decodeAudit(rawComment); audit.has_value()) {
        annotations.audit = *audit;
    }
    annotations.comment = PgnAnnotations::stripAuditTags(
        PgnAnnotations::stripAnnotationTags(rawComment));
    return annotations;
}

bool GameTree::parsePgn(const QString &pgn, const Rules &start, GameTree *out) {
    if (out == nullptr) {
        return false;
    }
    GameTree tree;

    // One frame per open variation: the position and node the next move
    // continues from, and the ones before the last move, which is where a
    // variation opened by "(" starts.
    struct Frame {
        int node = RootId;
        Rules position;
        bool hasPrevious = false;
        int previousNode = RootId;
        Rules previousPosition;
    };
    std::vector<Frame> stack;
    stack.push_back(Frame{RootId, start, false, RootId, start});
    QHash<int, QString> rawComments;

    const auto addComment = [&](const QString &text) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        QString &slot = rawComments[stack.back().node];
        if (!slot.isEmpty()) {
            slot += QLatin1Char(' ');
        }
        slot += trimmed;
    };

    const int n = pgn.size();
    int i = 0;
    int skippedDepth = 0;  // inside a variation that could not be anchored
    while (i < n) {
        const QChar ch = pgn[i];
        if (ch.isSpace()) {
            ++i;
            continue;
        }
        if (ch == QLatin1Char('[')) {
            while (i < n && pgn[i] != QLatin1Char(']')) {
                ++i;
            }
            ++i;
            continue;
        }
        if (ch == QLatin1Char('{')) {
            ++i;
            QString text;
            int depth = 1;
            while (i < n) {
                if (pgn[i] == QLatin1Char('{')) {
                    ++depth;
                } else if (pgn[i] == QLatin1Char('}') && --depth == 0) {
                    ++i;
                    break;
                }
                text += pgn[i++];
            }
            if (skippedDepth == 0) {
                addComment(text);
            }
            continue;
        }
        if (ch == QLatin1Char(';')) {
            ++i;
            QString text;
            while (i < n && pgn[i] != QLatin1Char('\n')) {
                text += pgn[i++];
            }
            if (skippedDepth == 0) {
                addComment(text);
            }
            continue;
        }
        if (ch == QLatin1Char('(')) {
            ++i;
            if (skippedDepth > 0 || !stack.back().hasPrevious) {
                ++skippedDepth;
            } else {
                const Frame &top = stack.back();
                Frame variation;
                variation.node = top.previousNode;
                variation.position = top.previousPosition;
                stack.push_back(std::move(variation));
            }
            continue;
        }
        if (ch == QLatin1Char(')')) {
            ++i;
            if (skippedDepth > 0) {
                --skippedDepth;
            } else if (stack.size() > 1) {
                stack.pop_back();
            }
            continue;
        }
        if (ch == QLatin1Char('$')) {
            ++i;
            while (i < n && pgn[i].isDigit()) {
                ++i;
            }
            continue;
        }

        const int tokenStart = i;
        while (i < n && !pgn[i].isSpace() && pgn[i] != QLatin1Char('{') &&
               pgn[i] != QLatin1Char(';') && pgn[i] != QLatin1Char('(') &&
               pgn[i] != QLatin1Char(')') && pgn[i] != QLatin1Char('[')) {
            ++i;
        }
        const QString token = pgn.mid(tokenStart, i - tokenStart);
        if (token.isEmpty()) {
            continue;
        }
        if (token == QLatin1String("1-0") || token == QLatin1String("0-1") ||
            token == QLatin1String("1/2-1/2") || token == QLatin1String("*")) {
            if (stack.size() == 1 && skippedDepth == 0) {
                break;
            }
            continue;
        }
        if (token.contains(QLatin1Char('.'))) {
            bool isMoveNumber = true;
            for (const QChar tc : token) {
                if (!tc.isDigit() && tc != QLatin1Char('.')) {
                    isMoveNumber = false;
                    break;
                }
            }
            if (isMoveNumber) {
                continue;
            }
        }
        if (skippedDepth > 0) {
            continue;
        }

        Frame &frame = stack.back();
        const auto move = frame.position.parseSan(token);
        if (!move.has_value()) {
            continue;
        }
        const QString uci = Rules::toUci(*move);
        Rules next = frame.position;
        if (!next.tryMove(*move)) {
            // The main line stops here, like the linear reader; a broken
            // variation just ends.
            if (stack.size() == 1) {
                break;
            }
            skippedDepth = 1;
            continue;
        }
        frame.previousNode = frame.node;
        frame.previousPosition = frame.position;
        frame.hasPrevious = true;
        frame.node = tree.addChild(frame.node, uci);
        frame.position = std::move(next);
    }

    for (auto it = rawComments.constBegin(); it != rawComments.constEnd(); ++it) {
        tree.nodes_[it.key()].annotations = decodeComment(it.value());
    }
    *out = std::move(tree);
    return true;
}
