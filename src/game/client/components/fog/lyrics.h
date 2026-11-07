#ifndef GAME_CLIENT_COMPONENTS_FOG_LYRICS_H
#define GAME_CLIENT_COMPONENTS_FOG_LYRICS_H

#include <cstdint>
#include <string>
#include <vector>

struct SLyricWord
{
	std::string m_Text;
	int64_t m_StartMs = 0;
	int64_t m_EndMs = 0;
	int m_NumLetters = 0; // UTF-8 codepoints that are not spaces
	int m_Syllables = 1; // vowel groups
	// where each letter finishes, as a fraction of the word's time: vowels are held longer
	// than consonants, so the highlight follows the singing instead of moving evenly
	std::vector<float> m_vLetterEnd;
};

struct SLyricLine
{
	int64_t m_StartMs = 0;
	int64_t m_EndMs = 0; // when the last word finishes being sung
	int64_t m_NextStartMs = 0; // when the next line starts (end of the gap)
	std::string m_Text;
	std::vector<SLyricWord> m_vWords;
};

// Synced lyrics parsed from LRC, including enhanced LRC word timings (<mm:ss.xx>).
// Lines without word timings get word and letter timings spread by letter count.
class CLyrics
{
public:
	bool Parse(const char *pLrc, int64_t TrackLengthMs);
	void Clear() { m_vLines.clear(); }
	bool Empty() const { return m_vLines.empty(); }

	const std::vector<SLyricLine> &Lines() const { return m_vLines; }
	// Index of the line being sung at TimeMs, -1 before the first line.
	int LineAt(int64_t TimeMs) const;

	// 0..1 of how far a word or a single letter of it has been sung at TimeMs.
	static float WordProgress(const SLyricWord &Word, int64_t TimeMs);
	static float LetterProgress(const SLyricWord &Word, int Letter, int64_t TimeMs);

private:
	std::vector<SLyricLine> m_vLines;
};

#endif
