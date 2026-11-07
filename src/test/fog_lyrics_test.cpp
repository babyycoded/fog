#include "test.h"

#include <game/client/components/fog/lyrics.h>

#include <gtest/gtest.h>

TEST(FogLyrics, ParsesLinesInOrder)
{
	CLyrics Lyrics;
	ASSERT_TRUE(Lyrics.Parse("[00:05.00]second\n[00:01.50]first line\n[ar:someone]\n", 10000));
	ASSERT_EQ(Lyrics.Lines().size(), 2u);
	EXPECT_EQ(Lyrics.Lines()[0].m_StartMs, 1500);
	EXPECT_EQ(Lyrics.Lines()[0].m_vWords.size(), 2u);
	EXPECT_EQ(Lyrics.Lines()[1].m_StartMs, 5000);
	EXPECT_EQ(Lyrics.LineAt(0), -1);
	EXPECT_EQ(Lyrics.LineAt(1500), 0);
	EXPECT_EQ(Lyrics.LineAt(6000), 1);
}

TEST(FogLyrics, VeryShortGapsDoNotCrash)
{
	// lines 100 ms apart used to cross the clamp bounds and abort
	CLyrics Lyrics;
	ASSERT_TRUE(Lyrics.Parse("[00:01.00]a long line with many words in it\n[00:01.10]next\n[00:01.10]same time\n[00:01.15]x\n", 2000));
	for(const SLyricLine &Line : Lyrics.Lines())
		for(const SLyricWord &Word : Line.m_vWords)
		{
			EXPECT_GE(Word.m_EndMs, Word.m_StartMs);
			EXPECT_LE(Word.m_StartMs, Line.m_NextStartMs);
		}
}

TEST(FogLyrics, EnhancedWordTimings)
{
	CLyrics Lyrics;
	ASSERT_TRUE(Lyrics.Parse("[00:02.00]<00:02.00>hello <00:02.50>world <00:03.40>\n", 10000));
	const SLyricLine &Line = Lyrics.Lines()[0];
	ASSERT_EQ(Line.m_vWords.size(), 2u);
	EXPECT_EQ(Line.m_vWords[0].m_StartMs, 2000);
	EXPECT_EQ(Line.m_vWords[0].m_EndMs, 2500);
	EXPECT_EQ(Line.m_vWords[1].m_EndMs, 3400);
	EXPECT_FLOAT_EQ(CLyrics::WordProgress(Line.m_vWords[0], 2250), 0.5f);
}

TEST(FogLyrics, OffsetAndTempoScale)
{
	CLyrics Lyrics;
	ASSERT_TRUE(Lyrics.Parse("[offset:+500]\n[fog_scale:2.0]\n[00:01.50]slowed\n", 10000));
	EXPECT_EQ(Lyrics.Lines()[0].m_StartMs, 2000); // (1500 - 500) * 2
}

TEST(FogLyrics, CyrillicLetterCount)
{
	CLyrics Lyrics;
	ASSERT_TRUE(Lyrics.Parse("[00:00.00]туман\n", 5000));
	EXPECT_EQ(Lyrics.Lines()[0].m_vWords[0].m_NumLetters, 5);
}
