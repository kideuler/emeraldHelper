#include "emerald/frameparser.h"

#include <gtest/gtest.h>

using emerald::FrameParser;
using emerald::RawSnapshot;

namespace {

void appendU16LE(QByteArray &buf, quint16 v)
{
    buf.append(char(v & 0xFF));
    buf.append(char((v >> 8) & 0xFF));
}

void appendU32LE(QByteArray &buf, quint32 v)
{
    appendU16LE(buf, quint16(v & 0xFFFF));
    appendU16LE(buf, quint16((v >> 16) & 0xFFFF));
}

// Builds one wire-format frame: [magic][version][regionCount]
// ([addr][len][bytes]) x N -- mirrors helperapp/lua/emerald_bridge.lua's
// framing exactly, since that script is this parser's only real producer.
QByteArray buildFrame(quint16 version, const QVector<QPair<quint32, QByteArray>> &regions)
{
    QByteArray out;
    out.append("EMBC", 4);
    appendU16LE(out, version);
    appendU16LE(out, quint16(regions.size()));
    for (const auto &region : regions) {
        appendU32LE(out, region.first);
        appendU32LE(out, quint32(region.second.size()));
        out.append(region.second);
    }
    return out;
}

} // namespace

TEST(FrameParser, ParsesASingleWholeFrame)
{
    const QVector<QPair<quint32, QByteArray>> regions = {
        {0x02001438, QByteArray("hello, battler")},
        {0x0200039c, QByteArray::fromHex("deadbeef")},
    };
    const QByteArray wire = buildFrame(1, regions);

    FrameParser parser;
    const QVector<RawSnapshot> frames = parser.feed(wire);

    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].version, 1);
    ASSERT_EQ(frames[0].regions.size(), 2);
    EXPECT_EQ(frames[0].regions[0].addr, 0x02001438u);
    EXPECT_EQ(frames[0].regions[0].bytes, QByteArray("hello, battler"));
    EXPECT_EQ(frames[0].regions[1].addr, 0x0200039cu);
    EXPECT_EQ(frames[0].regions[1].bytes, QByteArray::fromHex("deadbeef"));
    EXPECT_EQ(parser.pendingBytes(), 0);
}

TEST(FrameParser, SurvivesArbitraryByteAtATimeSplitting)
{
    const QVector<QPair<quint32, QByteArray>> regions = {
        {0x1, QByteArray(200, 'x')},
        {0x2, QByteArray(3, 'y')},
    };
    const QByteArray wire = buildFrame(7, regions);

    FrameParser parser;
    QVector<RawSnapshot> frames;
    for (char c : wire) {
        const auto batch = parser.feed(QByteArray(1, c));
        frames += batch;
    }

    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].version, 7);
    ASSERT_EQ(frames[0].regions.size(), 2);
    EXPECT_EQ(frames[0].regions[0].bytes.size(), 200);
    EXPECT_EQ(frames[0].regions[1].bytes, QByteArray(3, 'y'));
}

TEST(FrameParser, ParsesMultipleFramesDeliveredTogether)
{
    const QByteArray one = buildFrame(1, {{0xA, QByteArray("first")}});
    const QByteArray two = buildFrame(1, {{0xB, QByteArray("second")}});

    FrameParser parser;
    const QVector<RawSnapshot> frames = parser.feed(one + two);

    ASSERT_EQ(frames.size(), 2);
    EXPECT_EQ(frames[0].regions[0].bytes, QByteArray("first"));
    EXPECT_EQ(frames[1].regions[0].bytes, QByteArray("second"));
}

TEST(FrameParser, ResyncsPastGarbageBeforeAValidFrame)
{
    const QByteArray garbage = QByteArray("this is not a frame, no magic here");
    const QByteArray frame = buildFrame(1, {{0x42, QByteArray("payload")}});

    FrameParser parser;
    const QVector<RawSnapshot> frames = parser.feed(garbage + frame);

    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].regions[0].addr, 0x42u);
    EXPECT_EQ(frames[0].regions[0].bytes, QByteArray("payload"));
}

TEST(FrameParser, WaitsWhenAFrameIsIncomplete)
{
    const QByteArray wire = buildFrame(1, {{0x1, QByteArray(10, 'z')}});

    FrameParser parser;
    const QVector<RawSnapshot> frames = parser.feed(wire.left(wire.size() - 3));

    EXPECT_TRUE(frames.isEmpty());
    EXPECT_GT(parser.pendingBytes(), 0);
}
