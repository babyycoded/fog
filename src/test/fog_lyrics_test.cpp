#include "test.h"

#include <base/str.h>

#include <game/client/components/fog/lyrics.h>

#include <string>

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

TEST(FogLyrics, VowelsAreHeldLongerThanConsonants)
{
	CLyrics Lyrics;
	ASSERT_TRUE(Lyrics.Parse("[00:01.00]strong\n[00:05.00]next", 0));
	const SLyricWord &Word = Lyrics.Lines()[0].m_vWords[0];
	ASSERT_EQ(Word.m_vLetterEnd.size(), 6u);
	// s t r o n g: the vowel 'o' spans more of the word than the consonant 't'
	const float T = Word.m_vLetterEnd[1] - Word.m_vLetterEnd[0];
	const float O = Word.m_vLetterEnd[3] - Word.m_vLetterEnd[2];
	EXPECT_GT(O, T);
	EXPECT_FLOAT_EQ(Word.m_vLetterEnd.back(), 1.0f);
	// letters fill in order
	const int64_t Mid = (Word.m_StartMs + Word.m_EndMs) / 2;
	EXPECT_GE(CLyrics::LetterProgress(Word, 0, Mid), CLyrics::LetterProgress(Word, 5, Mid));
}

TEST(FogLyrics, TempoComesFromTheSong)
{
	// the same three-syllable line in a fast and a slow song: the fast one is sung quicker
	std::string Fast, Slow;
	for(int i = 0; i < 8; i++)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "[00:%02d.%02d]la la la la\n", (i * 120) / 100, (i * 120) % 100);
		Fast += aBuf;
		str_format(aBuf, sizeof(aBuf), "[00:%02d.00]la la la la\n", i * 3);
		Slow += aBuf;
	}
	Fast += "[00:30.00]end\n";
	Slow += "[00:40.00]end\n";
	CLyrics LyricsFast, LyricsSlow;
	ASSERT_TRUE(LyricsFast.Parse(Fast.c_str(), 0));
	ASSERT_TRUE(LyricsSlow.Parse(Slow.c_str(), 0));
	const SLyricLine &a = LyricsFast.Lines()[0], &b = LyricsSlow.Lines()[0];
	EXPECT_LT(a.m_EndMs - a.m_StartMs, b.m_EndMs - b.m_StartMs);
}
