// Unit tests for the opening book.

#include <QTest>

#include <algorithm>

#include "book.h"

class BookTest : public QObject {
    Q_OBJECT

private slots:
    void loadsBundledBook();
    void findsInitialOpeningMoves();
    void followsOpeningPosition();
    void handlesMissingBook();
};

void BookTest::loadsBundledBook() {
    const Book book;
    QVERIFY(book.isLoaded());
    QVERIFY(book.errorString().isEmpty());
}

void BookTest::findsInitialOpeningMoves() {
    const Book book;
    const Rules position;
    const QList<Book::Entry> moves = book.moves(position);

    QVERIFY(moves.size() >= 5);
    QVERIFY(std::any_of(moves.cbegin(), moves.cend(), [](const Book::Entry &entry) {
        return Rules::toUci(entry.move) == QStringLiteral("e2e4");
    }));
    const auto selected = book.pickMove(position);
    QVERIFY(selected.has_value());
    QCOMPARE(Rules::toUci(*selected), QStringLiteral("e2e4"));
}

void BookTest::followsOpeningPosition() {
    const Book book;
    Rules position;
    QVERIFY(position.tryMove({6, 4}, {4, 4}));

    const QList<Book::Entry> moves = book.moves(position);
    QVERIFY(std::any_of(moves.cbegin(), moves.cend(), [](const Book::Entry &entry) {
        return Rules::toUci(entry.move) == QStringLiteral("e7e5");
    }));
}

void BookTest::handlesMissingBook() {
    const Book book(QStringLiteral("/path/to/missing/book.bin"));
    QVERIFY(!book.isLoaded());
    QVERIFY(!book.errorString().isEmpty());
}

QTEST_MAIN(BookTest)
#include "book_test.moc"
