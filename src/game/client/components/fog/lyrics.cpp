#include "lyrics.h"

#include <base/math.h>
#include <base/str.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

// Parses "mm:ss", "mm:ss.xx" or "mm:ss.xxx" into milliseconds.
bool ParseTimestamp(const char *pStr, int Length, int64_t &OutMs)
{
	int Minutes = 0, i = 0;
	bool AnyDigit = false;
	while(i < Length && isdigit((unsigned char)pStr[i]))
	{
		Minutes = Minutes * 10 + (pStr[i] - '0');
		i++;
		AnyDigit = true;
	}
	if(!AnyDigit || i >= Length || pStr[i] != ':')
		return false;
	i++;
	int Seconds = 0, SecDigits = 0;
	while(i < Length && isdigit((unsigned char)pStr[i]))
	{
		Seconds = Seconds * 10 + (pStr[i] - '0');
		i++;
		SecDigits++;
	}
	if(SecDigits == 0)
		return false;
	int Fraction = 0, FracDigits = 0;
	if(i < Length && (pStr[i] == '.' || pStr[i] == ':'))
	{
		i++;
		while(i < Length && isdigit((unsigned char)pStr[i]))
		{
			if(FracDigits < 3)
			{
				Fraction = Fraction * 10 + (pStr[i] - '0');
				FracDigits++;
			}
			i++;
		}
	}
	if(i != Length)
		return false;
	while(FracDigits < 3)
	{
		Fraction *= 10;
		FracDigits++;
	}
	OutMs = (int64_t)Minutes * 60000 + (int64_t)Seconds * 1000 + Fraction;
	return true;
}

int CountLetters(const std::string &Text)
{
	int Count = 0;
	const char *p = Text.c_str();
	while(*p)
	{
		const int Codepoint = str_utf8_decode(&p);
		if(Codepoint > ' ')
			Count++;
		if(Codepoint <= 0)
			break;
	}
	return Count;
}

struct SRawWord
{
	std::string m_Text;
	int64_t m_StartMs = -1; // -1 when the word had no inline timestamp
};

struct SRawLine
{
	int64_t m_StartMs;
	std::vector<SRawWord> m_vWords;
	int64_t m_TrailingMs = -1; // enhanced LRC: timestamp after the last word marks its end
	int m_Order;
};

// Splits the line body into words, picking up enhanced LRC <mm:ss.xx> word timestamps.
void ParseBody(const char *pBody, SRawLine &Line)
{
	int64_t PendingMs = -1;
	std::string Current;
	int64_t CurrentStart = -1;
	auto Flush = [&]() {
		if(!Current.empty())
		{
			Line.m_vWords.push_back({Current, CurrentStart});
			Current.clear();
		}
		CurrentStart = -1;
	};

	const char *p = pBody;
	while(*p)
	{
		if(*p == '<')
		{
			const char *pEnd = str_find(p, ">");
			int64_t Ms;
			if(pEnd && ParseTimestamp(p + 1, (int)(pEnd - p - 1), Ms))
			{
				if(!Current.empty())
				{
					// a timestamp right after a word closes it
					Flush();
				}
				PendingMs = Ms;
				p = pEnd + 1;
				continue;
			}
		}
		if(*p == ' ' || *p == '\t')
		{
			Flush();
			p++;
			continue;
		}
		if(Current.empty())
		{
			CurrentStart = PendingMs;
			PendingMs = -1;
		}
		Current += *p;
		p++;
	}
	Flush();
	Line.m_TrailingMs = PendingMs;
}

} // namespace

bool CLyrics::Parse(const char *pLrc, int64_t TrackLengthMs)
{
	m_vLines.clear();
	if(!pLrc)
		return false;

	int64_t OffsetMs = 0;
	double TimeScale = 1.0; // [fog_scale:x], written for slowed / sped up edits
	std::vector<SRawLine> vRaw;

	const char *pLine = pLrc;
	while(*pLine)
	{
		const char *pLineEnd = pLine;
		while(*pLineEnd && *pLineEnd != '\n')
			pLineEnd++;
		std::string Line(pLine, pLineEnd - pLine);
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		pLine = *pLineEnd ? pLineEnd + 1 : pLineEnd;

		std::vector<int64_t> vTimes;
		size_t Pos = 0;
		while(Pos < Line.size() && Line[Pos] == '[')
		{
			const size_t Close = Line.find(']', Pos);
			if(Close == std::string::npos)
				break;
			const std::string Tag = Line.substr(Pos + 1, Close - Pos - 1);
			int64_t Ms;
			if(ParseTimestamp(Tag.c_str(), (int)Tag.size(), Ms))
				vTimes.push_back(Ms);
			else if(str_startswith_nocase(Tag.c_str(), "offset:"))
				OffsetMs = strtoll(Tag.c_str() + 7, nullptr, 10);
			else if(str_startswith_nocase(Tag.c_str(), "fog_scale:"))
			{
				const double Scale = strtod(Tag.c_str() + 10, nullptr);
				if(Scale > 0.25 && Scale < 4.0)
					TimeScale = Scale;
			}
			Pos = Close + 1;
		}
		if(vTimes.empty())
			continue;

		SRawLine Raw;
		Raw.m_StartMs = 0;
		Raw.m_Order = (int)vRaw.size();
		ParseBody(str_utf8_skip_whitespaces(Line.c_str() + Pos), Raw);
		for(int64_t Time : vTimes)
		{
			Raw.m_StartMs = Time;
			Raw.m_Order = (int)vRaw.size();
			vRaw.push_back(Raw);
		}
	}

	std::stable_sort(vRaw.begin(), vRaw.end(), [](const SRawLine &a, const SRawLine &b) {
		return a.m_StartMs != b.m_StartMs ? a.m_StartMs < b.m_StartMs : a.m_Order < b.m_Order;
	});

	// [offset:+N] shows lyrics N ms sooner; [fog_scale:x] stretches all timings
	const auto Adjust = [&](int64_t Ms) { return (int64_t)((Ms - OffsetMs) * TimeScale); };
	for(SRawLine &Raw : vRaw)
	{
		Raw.m_StartMs = Adjust(Raw.m_StartMs);
		for(SRawWord &Word : Raw.m_vWords)
			if(Word.m_StartMs >= 0)
				Word.m_StartMs = Adjust(Word.m_StartMs);
		if(Raw.m_TrailingMs >= 0)
			Raw.m_TrailingMs = Adjust(Raw.m_TrailingMs);
	}

	for(size_t i = 0; i < vRaw.size(); i++)
	{
		const SRawLine &Raw = vRaw[i];
		SLyricLine Line;
		Line.m_StartMs = Raw.m_StartMs;
		Line.m_NextStartMs = i + 1 < vRaw.size() ? vRaw[i + 1].m_StartMs : (TrackLengthMs > Raw.m_StartMs ? TrackLengthMs : Raw.m_StartMs + 8000);
		const int64_t Gap = std::max<int64_t>(Line.m_NextStartMs - Line.m_StartMs, 1);

		int TotalLetters = 0;
		for(const SRawWord &RawWord : Raw.m_vWords)
		{
			SLyricWord Word;
			Word.m_Text = RawWord.m_Text;
			Word.m_NumLetters = std::max(CountLetters(RawWord.m_Text), 1);
			TotalLetters += Word.m_NumLetters;
			if(!Line.m_Text.empty())
				Line.m_Text += ' ';
			Line.m_Text += RawWord.m_Text;
			Line.m_vWords.push_back(Word);
		}

		const bool AllTimed = !Raw.m_vWords.empty() && std::all_of(Raw.m_vWords.begin(), Raw.m_vWords.end(), [](const SRawWord &w) { return w.m_StartMs >= 0; });
		if(AllTimed)
		{
			for(size_t w = 0; w < Line.m_vWords.size(); w++)
			{
				SLyricWord &Word = Line.m_vWords[w];
				Word.m_StartMs = Raw.m_vWords[w].m_StartMs;
				int64_t End;
				if(w + 1 < Line.m_vWords.size())
					End = Raw.m_vWords[w + 1].m_StartMs;
				else if(Raw.m_TrailingMs >= 0)
					End = Raw.m_TrailingMs;
				else
					End = std::min<int64_t>(Word.m_StartMs + 120 + 90 * Word.m_NumLetters, Line.m_NextStartMs);
				Word.m_EndMs = std::max<int64_t>(End, Word.m_StartMs + 1);
			}
		}
		else if(!Line.m_vWords.empty())
		{
			// No word timings: assume a natural singing pace, never longer than the gap.
			// bounds can cross for very short gaps (lines < ~330 ms apart): the gap always wins
			const int64_t MinSpan = std::min<int64_t>(300, Gap);
			const int64_t MaxSpan = std::max<int64_t>(MinSpan, (int64_t)(Gap * 0.9f));
			const int64_t Span = std::clamp<int64_t>(350 + 75 * (int64_t)TotalLetters, MinSpan, MaxSpan);
			// Weight each word by its letters plus one for the breath between words.
			const int TotalWeight = TotalLetters + (int)Line.m_vWords.size();
			int64_t Cursor = Line.m_StartMs;
			int Accum = 0;
			for(SLyricWord &Word : Line.m_vWords)
			{
				Word.m_StartMs = Cursor;
				Accum += Word.m_NumLetters + 1;
				const int64_t Boundary = Line.m_StartMs + Span * Accum / TotalWeight;
				// the breath share sits after the word
				const int64_t BreathMs = Span / TotalWeight;
				Word.m_EndMs = std::max<int64_t>(Boundary - BreathMs, Word.m_StartMs + 1);
				Cursor = Boundary;
			}
		}

		Line.m_EndMs = Line.m_vWords.empty() ? Line.m_StartMs : Line.m_vWords.back().m_EndMs;
		m_vLines.push_back(std::move(Line));
	}

	return !m_vLines.empty();
}

int CLyrics::LineAt(int64_t TimeMs) const
{
	// first line whose start is after TimeMs, minus one
	auto It = std::upper_bound(m_vLines.begin(), m_vLines.end(), TimeMs, [](int64_t Time, const SLyricLine &Line) { return Time < Line.m_StartMs; });
	return (int)(It - m_vLines.begin()) - 1;
}

float CLyrics::WordProgress(const SLyricWord &Word, int64_t TimeMs)
{
	if(TimeMs <= Word.m_StartMs)
		return 0.0f;
	if(TimeMs >= Word.m_EndMs)
		return 1.0f;
	return (float)(TimeMs - Word.m_StartMs) / (float)(Word.m_EndMs - Word.m_StartMs);
}

float CLyrics::LetterProgress(const SLyricWord &Word, int Letter, int64_t TimeMs)
{
	const float Progress = WordProgress(Word, TimeMs) * Word.m_NumLetters;
	return std::clamp(Progress - Letter, 0.0f, 1.0f);
}
