#ifndef NOMINMAX
#define NOMINMAX
#endif

#define STB_VORBIS_HEADER_ONLY
#include "../modules/miniaudio/extras/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "../modules/miniaudio/miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "../modules/miniaudio/extras/stb_vorbis.c"

#include <gme/gme.h>

#include "CustomAudioPlayer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
	std::string toLowerAscii(std::string_view sv)
	{
		std::string out(sv);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return out;
	}

	std::string normalizeLoopKey(std::string_view key)
	{
		std::string out;
		out.reserve(key.size());
		for (unsigned char c : key)
		{
			if (std::isalnum(c))
			{
				out.push_back((char)std::toupper(c));
			}
		}
		return out;
	}

	std::string_view trimView(std::string_view sv)
	{
		while (!sv.empty() && std::isspace((unsigned char)sv.front()))
		{
			sv.remove_prefix(1);
		}
		while (!sv.empty() && std::isspace((unsigned char)sv.back()))
		{
			sv.remove_suffix(1);
		}
		return sv;
	}

	/// @brief Parse loop point value which may be sample frames ("123456"), seconds ("12.345s" / "12.345"),
	/// milliseconds ("12345ms"), or timestamp ("01:23.456")
	std::optional<std::uint64_t> parseLoopPointValue(std::string_view rawVal, std::uint32_t sampleRate)
	{
		rawVal = trimView(rawVal);
		if (rawVal.empty())
		{
			return std::nullopt;
		}

		// Timestamp format mm:ss(.xxx) or hh:mm:ss(.xxx)
		if (rawVal.find(':') != std::string_view::npos)
		{
			if (sampleRate == 0)
			{
				return std::nullopt;
			}
			double totalSeconds = 0.0;
			std::size_t start = 0;
			while (start < rawVal.size())
			{
				std::size_t colon = rawVal.find(':', start);
				std::string part(rawVal.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start));
				try
				{
					double v = std::stod(part);
					if (v < 0.0)
					{
						return std::nullopt;
					}
					totalSeconds = totalSeconds * 60.0 + v;
				}
				catch (...)
				{
					return std::nullopt;
				}
				if (colon == std::string_view::npos)
				{
					break;
				}
				start = colon + 1;
			}
			return static_cast<std::uint64_t>(std::llround(totalSeconds * sampleRate));
		}

		std::string lowerVal = toLowerAscii(rawVal);

		// Milliseconds suffix "ms"
		if (lowerVal.ends_with("ms"))
		{
			if (sampleRate == 0)
			{
				return std::nullopt;
			}
			try
			{
				double ms = std::stod(lowerVal.substr(0, lowerVal.size() - 2));
				if (ms < 0.0)
				{
					return std::nullopt;
				}
				return static_cast<std::uint64_t>(std::llround((ms / 1000.0) * sampleRate));
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		// Samples suffix "samples" or "smp"
		if (lowerVal.ends_with("samples"))
		{
			lowerVal.resize(lowerVal.size() - 7);
		}
		else if (lowerVal.ends_with("smp"))
		{
			lowerVal.resize(lowerVal.size() - 3);
		}
		else if (lowerVal.ends_with("s"))
		{
			// Seconds suffix "s"
			if (sampleRate == 0)
			{
				return std::nullopt;
			}
			try
			{
				double sec = std::stod(lowerVal.substr(0, lowerVal.size() - 1));
				if (sec < 0.0)
				{
					return std::nullopt;
				}
				return static_cast<std::uint64_t>(std::llround(sec * sampleRate));
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		// Floating point seconds (contains '.')
		if (lowerVal.find('.') != std::string::npos)
		{
			if (sampleRate == 0)
			{
				return std::nullopt;
			}
			try
			{
				double sec = std::stod(lowerVal);
				if (sec < 0.0)
				{
					return std::nullopt;
				}
				return static_cast<std::uint64_t>(std::llround(sec * sampleRate));
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		// Pure integer sample count
		try
		{
			std::size_t idx = 0;
			unsigned long long frames = std::stoull(lowerVal, &idx, 10);
			return static_cast<std::uint64_t>(frames);
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	void parseCommentKeyValue(
		std::string_view comment,
		std::uint32_t sampleRate,
		std::optional<std::uint64_t> &outStart,
		std::optional<std::uint64_t> &outLength,
		std::optional<std::uint64_t> &outEnd)
	{
		std::size_t eqPos = comment.find('=');
		if (eqPos == std::string_view::npos)
		{
			return;
		}

		std::string normKey = normalizeLoopKey(comment.substr(0, eqPos));
		std::string_view valView = trimView(comment.substr(eqPos + 1));

		// Handle nested COMMENT=LOOPSTART=...
		if (normKey == "COMMENT" && valView.find('=') != std::string_view::npos)
		{
			parseCommentKeyValue(valView, sampleRate, outStart, outLength, outEnd);
			return;
		}

		if (normKey == "LOOPSTART" ||
			normKey == "LOOPPOINT" ||
			normKey == "LOOP" ||
			normKey == "XIPHCUELOOPSTART" ||
			normKey == "SMPLLOOPSTART" ||
			normKey == "LIPLOOPSTART")
		{
			if (auto v = parseLoopPointValue(valView, sampleRate))
			{
				outStart = v;
			}
		}
		else if (normKey == "LOOPLENGTH" ||
				 normKey == "XIPHCUELOOPLENGTH" ||
				 normKey == "SMPLLOOPLENGTH" ||
				 normKey == "LIPLOOPLENGTH")
		{
			if (auto v = parseLoopPointValue(valView, sampleRate))
			{
				outLength = v;
			}
		}
		else if (normKey == "LOOPEND" ||
				 normKey == "LOOPPOINTEND" ||
				 normKey == "XIPHCUELOOPEND" ||
				 normKey == "SMPLLOOPEND" ||
				 normKey == "LIPLOOPEND")
		{
			if (auto v = parseLoopPointValue(valView, sampleRate))
			{
				outEnd = v;
			}
		}
	}

	CustomAudioPlayer::LoopInfo extractOggLoopInfo(stb_vorbis *vorbis)
	{
		CustomAudioPlayer::LoopInfo info{};
		if (!vorbis)
		{
			return info;
		}

		stb_vorbis_info vinfo = stb_vorbis_get_info(vorbis);
		info.sampleRate = vinfo.sample_rate;
		info.totalFrames = stb_vorbis_stream_length_in_samples(vorbis);

		stb_vorbis_comment vcomment = stb_vorbis_get_comment(vorbis);
		std::optional<std::uint64_t> loopStart;
		std::optional<std::uint64_t> loopLength;
		std::optional<std::uint64_t> loopEnd;

		for (int i = 0; i < vcomment.comment_list_length; ++i)
		{
			if (vcomment.comment_list[i])
			{
				parseCommentKeyValue(vcomment.comment_list[i], info.sampleRate, loopStart, loopLength, loopEnd);
			}
		}

		if (loopStart.has_value())
		{
			info.hasLoopTag = true;
			info.loopStartFrame = *loopStart;

			if (loopLength.has_value() && *loopLength > 0)
			{
				info.loopEndFrame = info.loopStartFrame + *loopLength;
			}
			else if (loopEnd.has_value() && *loopEnd > 0)
			{
				// Compatibility: if someone mistakenly wrote LOOPEND as length where loopEnd <= loopStart
				if (*loopEnd <= info.loopStartFrame)
				{
					info.loopEndFrame = info.loopStartFrame + *loopEnd;
				}
				else
				{
					info.loopEndFrame = *loopEnd;
				}
			}
			else
			{
				info.loopEndFrame = info.totalFrames;
			}
		}
		else if (loopEnd.has_value() && *loopEnd > 0)
		{
			info.hasLoopTag = true;
			info.loopStartFrame = 0;
			info.loopEndFrame = *loopEnd;
		}
		else if (loopLength.has_value() && *loopLength > 0 && info.totalFrames > *loopLength)
		{
			info.hasLoopTag = true;
			info.loopStartFrame = info.totalFrames - *loopLength;
			info.loopEndFrame = info.totalFrames;
		}

		if (info.totalFrames > 0)
		{
			if (info.loopEndFrame == 0 || info.loopEndFrame > info.totalFrames)
			{
				info.loopEndFrame = info.totalFrames;
			}
			if (info.loopStartFrame >= info.loopEndFrame)
			{
				info.loopStartFrame = 0;
			}
		}

		return info;
	}

	/// @brief Parse RIFF WAV "smpl" chunk if present for seamless WAV loop point support
	std::optional<CustomAudioPlayer::LoopInfo> extractWavSmplLoopInfo(const std::filesystem::path &path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return std::nullopt;
		}

		char riffHeader[12];
		if (!file.read(riffHeader, 12))
		{
			return std::nullopt;
		}
		if (std::memcmp(riffHeader, "RIFF", 4) != 0 || std::memcmp(riffHeader + 8, "WAVE", 4) != 0)
		{
			return std::nullopt;
		}

		CustomAudioPlayer::LoopInfo info{};
		std::uint32_t bytesPerFrame = 0;
		std::uint32_t dataChunkBytes = 0;

		while (file)
		{
			char chunkId[4];
			std::uint32_t chunkSize = 0;
			if (!file.read(chunkId, 4) || !file.read(reinterpret_cast<char *>(&chunkSize), 4))
			{
				break;
			}

			std::streampos nextChunkPos = file.tellg() + static_cast<std::streamoff>(chunkSize + (chunkSize & 1));

			if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16)
			{
				std::uint16_t audioFormat = 0;
				std::uint16_t numChannels = 0;
				std::uint32_t sampleRate = 0;
				std::uint32_t byteRate = 0;
				std::uint16_t blockAlign = 0;
				file.read(reinterpret_cast<char *>(&audioFormat), 2);
				file.read(reinterpret_cast<char *>(&numChannels), 2);
				file.read(reinterpret_cast<char *>(&sampleRate), 4);
				file.read(reinterpret_cast<char *>(&byteRate), 4);
				file.read(reinterpret_cast<char *>(&blockAlign), 2);
				info.sampleRate = sampleRate;
				bytesPerFrame = blockAlign;
			}
			else if (std::memcmp(chunkId, "data", 4) == 0)
			{
				dataChunkBytes = chunkSize;
			}
			else if (std::memcmp(chunkId, "smpl", 4) == 0 && chunkSize >= 36 + 24)
			{
				std::uint32_t smplHeader[9]{};
				if (file.read(reinterpret_cast<char *>(smplHeader), 36))
				{
					std::uint32_t numSampleLoops = smplHeader[7];
					if (numSampleLoops > 0)
					{
						std::uint32_t loopFields[6]{};
						if (file.read(reinterpret_cast<char *>(loopFields), 24))
						{
							std::uint32_t startFrame = loopFields[2];
							std::uint32_t endFrame = loopFields[3];
							if (endFrame > startFrame)
							{
								info.hasLoopTag = true;
								info.loopStartFrame = startFrame;
								info.loopEndFrame = static_cast<std::uint64_t>(endFrame) + 1;
							}
						}
					}
				}
			}

			file.seekg(nextChunkPos);
		}

		if (bytesPerFrame > 0 && dataChunkBytes > 0)
		{
			info.totalFrames = dataChunkBytes / bytesPerFrame;
			if (info.loopEndFrame == 0 || info.loopEndFrame > info.totalFrames)
			{
				info.loopEndFrame = info.totalFrames;
			}
		}

		return info;
	}

	// =========================================================================
	// Minimal RFC1951 (Deflate) + RFC1952 (Gzip) decompressor for .vgz support
	// =========================================================================
	struct BitReader
	{
		const std::uint8_t *data;
		std::size_t size;
		std::size_t bytePos = 0;
		std::uint32_t bitBuf = 0;
		int bitCount = 0;

		bool ensureBits(int n)
		{
			while (bitCount < n)
			{
				if (bytePos >= size)
				{
					return false;
				}
				bitBuf |= static_cast<std::uint32_t>(data[bytePos++]) << bitCount;
				bitCount += 8;
			}
			return true;
		}

		std::uint32_t readBits(int n)
		{
			ensureBits(n);
			std::uint32_t val = bitBuf & ((1u << n) - 1u);
			bitBuf >>= n;
			bitCount -= n;
			return val;
		}

		void alignToByte()
		{
			bitBuf = 0;
			bitCount = 0;
		}
	};

	struct HuffmanTable
	{
		std::array<std::uint16_t, 16> counts{};
		std::array<std::uint16_t, 288> symbols{};

		bool build(const std::uint8_t *lengths, int numSymbols)
		{
			counts.fill(0);
			for (int i = 0; i < numSymbols; ++i)
			{
				if (lengths[i] > 15)
				{
					return false;
				}
				counts[lengths[i]]++;
			}
			counts[0] = 0;

			std::array<std::uint16_t, 16> offsets{};
			std::uint16_t sum = 0;
			for (int len = 1; len <= 15; ++len)
			{
				offsets[len] = sum;
				sum += counts[len];
			}

			for (int i = 0; i < numSymbols; ++i)
			{
				if (lengths[i] != 0)
				{
					symbols[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
				}
			}
			return true;
		}

		int decode(BitReader &br) const
		{
			int code = 0;
			int first = 0;
			int index = 0;
			for (int len = 1; len <= 15; ++len)
			{
				if (!br.ensureBits(1))
				{
					return -1;
				}
				code |= static_cast<int>(br.readBits(1));
				int count = counts[len];
				if (code - count < first)
				{
					return symbols[index + (code - first)];
				}
				index += count;
				first += count;
				first <<= 1;
				code <<= 1;
			}
			return -1;
		}
	};

	bool decompressGzipIfNeeded(std::vector<std::uint8_t> &buffer)
	{
		if (buffer.size() < 18 || buffer[0] != 0x1F || buffer[1] != 0x8B || buffer[2] != 0x08)
		{
			return true; // Not gzip, use raw buffer as-is
		}

		std::uint8_t flags = buffer[3];
		std::size_t pos = 10;
		if (flags & 0x04) // FEXTRA
		{
			if (pos + 2 > buffer.size())
				return false;
			std::uint16_t xlen = buffer[pos] | (buffer[pos + 1] << 8);
			pos += 2 + xlen;
		}
		if (flags & 0x08) // FNAME
		{
			while (pos < buffer.size() && buffer[pos] != 0)
				pos++;
			pos++;
		}
		if (flags & 0x10) // FCOMMENT
		{
			while (pos < buffer.size() && buffer[pos] != 0)
				pos++;
			pos++;
		}
		if (flags & 0x02) // FHCRC
		{
			pos += 2;
		}
		if (pos >= buffer.size() - 8)
		{
			return false;
		}

		std::uint32_t expectedSize =
			static_cast<std::uint32_t>(buffer[buffer.size() - 4]) |
			(static_cast<std::uint32_t>(buffer[buffer.size() - 3]) << 8) |
			(static_cast<std::uint32_t>(buffer[buffer.size() - 2]) << 16) |
			(static_cast<std::uint32_t>(buffer[buffer.size() - 1]) << 24);

		std::vector<std::uint8_t> out;
		out.reserve(std::min<std::size_t>(expectedSize, 32 * 1024 * 1024));

		BitReader br{buffer.data() + pos, buffer.size() - pos - 8};

		static constexpr int LENGTH_BASE[29] = {
			3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
			35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
		static constexpr int LENGTH_EXTRA[29] = {
			0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
			3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
		static constexpr int DIST_BASE[30] = {
			1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
			257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
		static constexpr int DIST_EXTRA[30] = {
			0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
			7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
		static constexpr int CLEN_ORDER[19] = {
			16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

		bool bfinal = false;
		while (!bfinal)
		{
			if (!br.ensureBits(3))
				return false;
			bfinal = br.readBits(1) != 0;
			std::uint32_t btype = br.readBits(2);

			if (btype == 0)
			{
				br.alignToByte();
				if (br.bytePos + 4 > br.size)
					return false;
				std::uint16_t len = br.data[br.bytePos] | (br.data[br.bytePos + 1] << 8);
				std::uint16_t nlen = br.data[br.bytePos + 2] | (br.data[br.bytePos + 3] << 8);
				br.bytePos += 4;
				if ((len ^ 0xFFFF) != nlen || br.bytePos + len > br.size)
					return false;
				out.insert(out.end(), br.data + br.bytePos, br.data + br.bytePos + len);
				br.bytePos += len;
			}
			else if (btype == 1 || btype == 2)
			{
				HuffmanTable litLenTable{};
				HuffmanTable distTable{};

				if (btype == 1)
				{
					std::array<std::uint8_t, 288> litLengths{};
					for (int i = 0; i <= 143; ++i)
						litLengths[i] = 8;
					for (int i = 144; i <= 255; ++i)
						litLengths[i] = 9;
					for (int i = 256; i <= 279; ++i)
						litLengths[i] = 7;
					for (int i = 280; i <= 287; ++i)
						litLengths[i] = 8;
					litLenTable.build(litLengths.data(), 288);

					std::array<std::uint8_t, 32> distLengths{};
					distLengths.fill(5);
					distTable.build(distLengths.data(), 32);
				}
				else
				{
					if (!br.ensureBits(14))
						return false;
					int hlit = static_cast<int>(br.readBits(5)) + 257;
					int hdist = static_cast<int>(br.readBits(5)) + 1;
					int hclen = static_cast<int>(br.readBits(4)) + 4;

					std::array<std::uint8_t, 19> clenLengths{};
					for (int i = 0; i < hclen; ++i)
					{
						if (!br.ensureBits(3))
							return false;
						clenLengths[CLEN_ORDER[i]] = static_cast<std::uint8_t>(br.readBits(3));
					}

					HuffmanTable codeLenTable{};
					if (!codeLenTable.build(clenLengths.data(), 19))
						return false;

					std::array<std::uint8_t, 320> allLengths{};
					int idx = 0;
					while (idx < hlit + hdist)
					{
						int sym = codeLenTable.decode(br);
						if (sym < 0)
							return false;
						if (sym < 16)
						{
							allLengths[idx++] = static_cast<std::uint8_t>(sym);
						}
						else if (sym == 16)
						{
							if (idx == 0 || !br.ensureBits(2))
								return false;
							int rep = static_cast<int>(br.readBits(2)) + 3;
							std::uint8_t prev = allLengths[idx - 1];
							while (rep-- > 0 && idx < hlit + hdist)
								allLengths[idx++] = prev;
						}
						else if (sym == 17)
						{
							if (!br.ensureBits(3))
								return false;
							int rep = static_cast<int>(br.readBits(3)) + 3;
							while (rep-- > 0 && idx < hlit + hdist)
								allLengths[idx++] = 0;
						}
						else if (sym == 18)
						{
							if (!br.ensureBits(7))
								return false;
							int rep = static_cast<int>(br.readBits(7)) + 11;
							while (rep-- > 0 && idx < hlit + hdist)
								allLengths[idx++] = 0;
						}
					}

					if (!litLenTable.build(allLengths.data(), hlit) ||
						!distTable.build(allLengths.data() + hlit, hdist))
					{
						return false;
					}
				}

				while (true)
				{
					int sym = litLenTable.decode(br);
					if (sym < 0)
						return false;
					if (sym < 256)
					{
						out.push_back(static_cast<std::uint8_t>(sym));
					}
					else if (sym == 256)
					{
						break;
					}
					else
					{
						int lenIdx = sym - 257;
						if (lenIdx < 0 || lenIdx >= 29)
							return false;
						int length = LENGTH_BASE[lenIdx];
						int extraBits = LENGTH_EXTRA[lenIdx];
						if (extraBits > 0)
						{
							if (!br.ensureBits(extraBits))
								return false;
							length += static_cast<int>(br.readBits(extraBits));
						}

						int distSym = distTable.decode(br);
						if (distSym < 0 || distSym >= 30)
							return false;
						std::size_t dist = DIST_BASE[distSym];
						int distExtra = DIST_EXTRA[distSym];
						if (distExtra > 0)
						{
							if (!br.ensureBits(distExtra))
								return false;
							dist += br.readBits(distExtra);
						}

						if (dist == 0 || dist > out.size())
							return false;

						for (int i = 0; i < length; ++i)
						{
							out.push_back(out[out.size() - dist]);
						}
					}
				}
			}
			else
			{
				return false;
			}
		}

		buffer = std::move(out);
		return true;
	}

	// =========================================================================
	// Pull-Mode Ogg Vorbis Data Source with Seamless Loop Points
	// =========================================================================
	struct OggLoopDataSource
	{
		ma_data_source_base base{};
		FILE *file = nullptr;
		stb_vorbis *vorbis = nullptr;
		std::uint32_t channels = 2;
		std::uint32_t sampleRate = 44100;
		std::uint64_t cursor = 0;
		bool looping = true;
		CustomAudioPlayer::LoopInfo loopInfo{};

		static ma_result onRead(ma_data_source *pDataSource, void *pFramesOut, ma_uint64 frameCount, ma_uint64 *pFramesRead)
		{
			auto *self = static_cast<OggLoopDataSource *>(pDataSource);
			if (pFramesRead)
			{
				*pFramesRead = 0;
			}
			if (!self || !self->vorbis || frameCount == 0)
			{
				return MA_INVALID_ARGS;
			}

			float *outF32 = static_cast<float *>(pFramesOut);
			ma_uint64 totalRead = 0;
			const std::uint64_t effectiveLoopEnd = (self->loopInfo.loopEndFrame > 0) ? self->loopInfo.loopEndFrame : self->loopInfo.totalFrames;
			bool justSought = false;

			while (totalRead < frameCount)
			{
				if (self->looping && effectiveLoopEnd > 0 && self->cursor >= effectiveLoopEnd)
				{
					stb_vorbis_seek(self->vorbis, static_cast<unsigned int>(self->loopInfo.loopStartFrame));
					self->cursor = self->loopInfo.loopStartFrame;
					justSought = true;
				}

				ma_uint64 framesToRead = frameCount - totalRead;
				if (self->looping && effectiveLoopEnd > self->cursor)
				{
					framesToRead = std::min<ma_uint64>(framesToRead, effectiveLoopEnd - self->cursor);
				}

				int chunkFrames = static_cast<int>(std::min<ma_uint64>(framesToRead, 65536));
				float *dst = outF32 ? (outF32 + totalRead * self->channels) : nullptr;

				int gotFrames = 0;
				if (dst)
				{
					gotFrames = stb_vorbis_get_samples_float_interleaved(
						self->vorbis,
						static_cast<int>(self->channels),
						dst,
						chunkFrames * static_cast<int>(self->channels));
				}
				else
				{
					std::vector<float> discard(chunkFrames * self->channels);
					gotFrames = stb_vorbis_get_samples_float_interleaved(
						self->vorbis,
						static_cast<int>(self->channels),
						discard.data(),
						chunkFrames * static_cast<int>(self->channels));
				}

				if (gotFrames > 0)
				{
					totalRead += static_cast<ma_uint64>(gotFrames);
					self->cursor += static_cast<ma_uint64>(gotFrames);
					justSought = false;
				}
				else
				{
					if (self->looping && !justSought)
					{
						stb_vorbis_seek(self->vorbis, static_cast<unsigned int>(self->loopInfo.loopStartFrame));
						self->cursor = self->loopInfo.loopStartFrame;
						justSought = true;
					}
					else
					{
						break;
					}
				}
			}

			if (pFramesRead)
			{
				*pFramesRead = totalRead;
			}
			return (totalRead == 0) ? MA_AT_END : MA_SUCCESS;
		}

		static ma_result onSeek(ma_data_source *pDataSource, ma_uint64 frameIndex)
		{
			auto *self = static_cast<OggLoopDataSource *>(pDataSource);
			if (!self || !self->vorbis)
			{
				return MA_INVALID_ARGS;
			}
			if (stb_vorbis_seek(self->vorbis, static_cast<unsigned int>(frameIndex)) == 0)
			{
				return MA_ERROR;
			}
			self->cursor = frameIndex;
			return MA_SUCCESS;
		}

		static ma_result onGetDataFormat(ma_data_source *pDataSource, ma_format *pFormat, ma_uint32 *pChannels, ma_uint32 *pSampleRate, ma_channel *pChannelMap, size_t channelMapCap)
		{
			auto *self = static_cast<OggLoopDataSource *>(pDataSource);
			if (pFormat)
				*pFormat = ma_format_f32;
			if (pChannels)
				*pChannels = self->channels;
			if (pSampleRate)
				*pSampleRate = self->sampleRate;
			if (pChannelMap)
				ma_channel_map_init_standard(ma_standard_channel_map_default, pChannelMap, channelMapCap, self->channels);
			return MA_SUCCESS;
		}

		static ma_result onGetCursor(ma_data_source *pDataSource, ma_uint64 *pCursor)
		{
			auto *self = static_cast<OggLoopDataSource *>(pDataSource);
			if (pCursor)
				*pCursor = self->cursor;
			return MA_SUCCESS;
		}

		static ma_result onGetLength(ma_data_source *pDataSource, ma_uint64 *pLength)
		{
			auto *self = static_cast<OggLoopDataSource *>(pDataSource);
			if (pLength)
				*pLength = self->loopInfo.totalFrames;
			return MA_SUCCESS;
		}

		static ma_result onSetLooping(ma_data_source *pDataSource, ma_bool32 isLooping)
		{
			auto *self = static_cast<OggLoopDataSource *>(pDataSource);
			self->looping = (isLooping != MA_FALSE);
			return MA_SUCCESS;
		}

		static inline const ma_data_source_vtable s_vtable = {
			onRead,
			onSeek,
			onGetDataFormat,
			onGetCursor,
			onGetLength,
			onSetLooping,
			0};

		bool open(const std::filesystem::path &path, bool shouldLoop)
		{
			file = _wfopen(path.c_str(), L"rb");
			if (!file)
			{
				return false;
			}

			int error = 0;
			vorbis = stb_vorbis_open_file(file, FALSE, &error, nullptr);
			if (!vorbis)
			{
				std::fclose(file);
				file = nullptr;
				return false;
			}

			stb_vorbis_info vinfo = stb_vorbis_get_info(vorbis);
			channels = static_cast<std::uint32_t>(vinfo.channels);
			sampleRate = static_cast<std::uint32_t>(vinfo.sample_rate);
			loopInfo = extractOggLoopInfo(vorbis);
			looping = shouldLoop;
			cursor = 0;

			ma_data_source_config cfg = ma_data_source_config_init();
			cfg.vtable = &s_vtable;
			if (ma_data_source_init(&cfg, &base) != MA_SUCCESS)
			{
				stb_vorbis_close(vorbis);
				vorbis = nullptr;
				std::fclose(file);
				file = nullptr;
				return false;
			}

			return true;
		}

		~OggLoopDataSource()
		{
			if (vorbis)
			{
				ma_data_source_uninit(&base);
				stb_vorbis_close(vorbis);
				vorbis = nullptr;
			}
			if (file)
			{
				std::fclose(file);
				file = nullptr;
			}
		}
	};

	// =========================================================================
	// Game_Music_Emu (libgme) Data Source for .nsf, .spc, .vgm, .vgz, .gbs, etc.
	// =========================================================================
	struct GmeDataSource
	{
		ma_data_source_base base{};
		Music_Emu *emu = nullptr;
		std::uint32_t sampleRate = 44100;
		std::uint64_t cursor = 0;
		bool looping = true;
		int trackIndex = 0;

		static ma_result onRead(ma_data_source *pDataSource, void *pFramesOut, ma_uint64 frameCount, ma_uint64 *pFramesRead)
		{
			auto *self = static_cast<GmeDataSource *>(pDataSource);
			if (pFramesRead)
			{
				*pFramesRead = 0;
			}
			if (!self || !self->emu || frameCount == 0)
			{
				return MA_INVALID_ARGS;
			}

			if (!self->looping && gme_track_ended(self->emu))
			{
				return MA_AT_END;
			}

			short *outS16 = static_cast<short *>(pFramesOut);
			const int sampleCount = static_cast<int>(frameCount * 2); // Stereo (2 channels)
			if (outS16)
			{
				if (gme_play(self->emu, sampleCount, outS16) != nullptr)
				{
					return MA_ERROR;
				}
			}
			else
			{
				std::vector<short> discard(sampleCount);
				if (gme_play(self->emu, sampleCount, discard.data()) != nullptr)
				{
					return MA_ERROR;
				}
			}

			if (self->looping && gme_track_ended(self->emu))
			{
				gme_start_track(self->emu, self->trackIndex);
			}

			self->cursor += frameCount;
			if (pFramesRead)
			{
				*pFramesRead = frameCount;
			}
			return MA_SUCCESS;
		}

		static ma_result onSeek(ma_data_source *pDataSource, ma_uint64 frameIndex)
		{
			auto *self = static_cast<GmeDataSource *>(pDataSource);
			if (!self || !self->emu)
			{
				return MA_INVALID_ARGS;
			}
			long msec = static_cast<long>((frameIndex * 1000ULL) / self->sampleRate);
			if (gme_seek(self->emu, msec) != nullptr)
			{
				return MA_ERROR;
			}
			self->cursor = frameIndex;
			return MA_SUCCESS;
		}

		static ma_result onGetDataFormat(ma_data_source *pDataSource, ma_format *pFormat, ma_uint32 *pChannels, ma_uint32 *pSampleRate, ma_channel *pChannelMap, size_t channelMapCap)
		{
			auto *self = static_cast<GmeDataSource *>(pDataSource);
			if (pFormat)
				*pFormat = ma_format_s16;
			if (pChannels)
				*pChannels = 2;
			if (pSampleRate)
				*pSampleRate = self->sampleRate;
			if (pChannelMap)
				ma_channel_map_init_standard(ma_standard_channel_map_default, pChannelMap, channelMapCap, 2);
			return MA_SUCCESS;
		}

		static ma_result onGetCursor(ma_data_source *pDataSource, ma_uint64 *pCursor)
		{
			auto *self = static_cast<GmeDataSource *>(pDataSource);
			if (pCursor)
				*pCursor = self->cursor;
			return MA_SUCCESS;
		}

		static ma_result onGetLength(ma_data_source *pDataSource, ma_uint64 *pLength)
		{
			if (pLength)
				*pLength = 0;
			return MA_NOT_IMPLEMENTED;
		}

		static ma_result onSetLooping(ma_data_source *pDataSource, ma_bool32 isLooping)
		{
			auto *self = static_cast<GmeDataSource *>(pDataSource);
			self->looping = (isLooping != MA_FALSE);
			if (self->emu)
			{
				gme_set_autoload_playback_limit(self->emu, self->looping ? 0 : 1);
			}
			return MA_SUCCESS;
		}

		static inline const ma_data_source_vtable s_vtable = {
			onRead,
			onSeek,
			onGetDataFormat,
			onGetCursor,
			onGetLength,
			onSetLooping,
			0};

		bool open(const std::filesystem::path &resolvedPath, int subTrack, bool shouldLoop, std::uint32_t targetSampleRate = 44100)
		{
			std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
			if (!file)
			{
				return false;
			}
			std::streamsize fileSize = file.tellg();
			if (fileSize <= 0)
			{
				return false;
			}
			file.seekg(0, std::ios::beg);

			std::vector<std::uint8_t> data(static_cast<std::size_t>(fileSize));
			if (!file.read(reinterpret_cast<char *>(data.data()), fileSize))
			{
				return false;
			}

			// Transparently decompress .vgz (or gzipped chiptune files)
			if (!decompressGzipIfNeeded(data) || data.empty())
			{
				return false;
			}

			sampleRate = targetSampleRate;
			if (gme_open_data(data.data(), static_cast<long>(data.size()), &emu, sampleRate) != nullptr || !emu)
			{
				return false;
			}

			looping = shouldLoop;
			gme_ignore_silence(emu, 1);
			gme_set_autoload_playback_limit(emu, looping ? 0 : 1);

			int totalTracks = gme_track_count(emu);
			trackIndex = std::clamp(subTrack, 0, std::max(0, totalTracks - 1));

			if (gme_start_track(emu, trackIndex) != nullptr)
			{
				gme_delete(emu);
				emu = nullptr;
				return false;
			}

			ma_data_source_config cfg = ma_data_source_config_init();
			cfg.vtable = &s_vtable;
			if (ma_data_source_init(&cfg, &base) != MA_SUCCESS)
			{
				gme_delete(emu);
				emu = nullptr;
				return false;
			}

			return true;
		}

		~GmeDataSource()
		{
			if (emu)
			{
				ma_data_source_uninit(&base);
				gme_delete(emu);
				emu = nullptr;
			}
		}
	};

	bool isGmeExtension(std::string_view extLower)
	{
		static const std::unordered_set<std::string_view> s_gmeExts = {
			".nsf", ".nsfe", ".spc", ".vgm", ".vgz", ".gbs", ".hes", ".ay", ".kss", ".sap", ".gym"};
		return s_gmeExts.contains(extLower);
	}

	bool isStandardExtension(std::string_view extLower)
	{
		static const std::unordered_set<std::string_view> s_stdExts = {
			".ogg", ".mp3", ".wav", ".flac"};
		return s_stdExts.contains(extLower);
	}

	/// @brief Per-format loudness normalization factor to offset libgme's built-in gain boosts
	/// (e.g. Nsf_Emu/Spc_Emu set_gain(1.4), Gbs_Emu set_gain(1.2)) and square-wave crest factor,
	/// aligning chiptune formats with standard mastered .ogg/.mp3/.wav tracks.
	float getFormatGain(std::string_view extLower)
	{
		if (extLower == ".nsf" || extLower == ".nsfe")
		{
			return 0.65f;
		}
		if (extLower == ".spc")
		{
			return 0.714f;
		}
		if (extLower == ".gbs" || extLower == ".ay" || extLower == ".kss" || extLower == ".sap")
		{
			return 0.78f;
		}
		if (extLower == ".hes" || extLower == ".vgm" || extLower == ".vgz" || extLower == ".gym")
		{
			return 0.88f;
		}
		return 1.0f;
	}

	/// @brief Resolve optional "#track" suffix in filename (e.g. "MM2#3.nsf" -> path "MM2.nsf" or "MM2#3.nsf", trackIndex = 2 (0-based))
	void resolvePathAndSubTrack(const std::filesystem::path &inputPath, std::filesystem::path &outActualPath, int &outSubTrackIndex)
	{
		outActualPath = inputPath;
		outSubTrackIndex = 0;

		std::wstring stem = inputPath.stem().wstring();
		std::size_t hashPos = stem.rfind(L'#');
		if (hashPos != std::wstring::npos && hashPos + 1 < stem.size())
		{
			std::wstring numPart = stem.substr(hashPos + 1);
			bool allDigits = !numPart.empty() && std::ranges::all_of(numPart, [](wchar_t c) { return std::iswdigit(c) != 0; });
			if (allDigits)
			{
				try
				{
					int parsedTrack = std::stoi(numPart);
					// User-friendly 1-based track number (#1 -> 0, #2 -> 1), with #0 also mapping to 0
					outSubTrackIndex = (parsedTrack > 0) ? (parsedTrack - 1) : 0;

					if (!std::filesystem::exists(inputPath))
					{
						std::filesystem::path strippedPath = inputPath.parent_path() / (stem.substr(0, hashPos) + inputPath.extension().wstring());
						if (std::filesystem::exists(strippedPath))
						{
							outActualPath = strippedPath;
						}
					}
				}
				catch (...)
				{
				}
			}
		}
	}
} // namespace

struct CustomAudioPlayer::SlotState
{
	std::unique_ptr<OggLoopDataSource> oggSource;
	std::unique_ptr<GmeDataSource> gmeSource;
	std::unique_ptr<ma_decoder> stdDecoder;
	std::unique_ptr<ma_sound> sound;

	bool active = false;
	bool paused = false;
	bool fadingOut = false;
	std::chrono::steady_clock::time_point fadeEndTime{};
	TrackMetadata metadata{};

	void cleanup()
	{
		if (sound)
		{
			ma_sound_stop(sound.get());
			ma_sound_uninit(sound.get());
			sound.reset();
		}
		if (stdDecoder)
		{
			ma_decoder_uninit(stdDecoder.get());
			stdDecoder.reset();
		}
		oggSource.reset();
		gmeSource.reset();
		active = false;
		paused = false;
		fadingOut = false;
		metadata = {};
	}

	~SlotState()
	{
		cleanup();
	}
};

CustomAudioPlayer &CustomAudioPlayer::instance()
{
	static CustomAudioPlayer s_instance;
	return s_instance;
}

CustomAudioPlayer::~CustomAudioPlayer()
{
	shutdown();
}

bool CustomAudioPlayer::init()
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (m_initialized)
	{
		return true;
	}

	m_engine = new ma_engine{};
	ma_engine_config engineConfig = ma_engine_config_init();
	if (ma_engine_init(&engineConfig, m_engine) != MA_SUCCESS)
	{
		delete m_engine;
		m_engine = nullptr;
		return false;
	}

	ma_engine_set_volume(m_engine, m_masterVolume);
	m_slots.resize(4);
	for (auto &slot : m_slots)
	{
		slot = std::make_unique<SlotState>();
	}

	m_initialized = true;
	return true;
}

void CustomAudioPlayer::shutdown()
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (!m_initialized)
	{
		return;
	}

	for (auto &slot : m_slots)
	{
		if (slot)
		{
			slot->cleanup();
		}
	}
	m_slots.clear();

	if (m_engine)
	{
		ma_engine_uninit(m_engine);
		delete m_engine;
		m_engine = nullptr;
	}

	m_initialized = false;
}

bool CustomAudioPlayer::isSupportedAudioFile(const std::filesystem::path &path)
{
	std::string ext = toLowerAscii(path.extension().string());
	return isStandardExtension(ext) || isGmeExtension(ext);
}

std::optional<CustomAudioPlayer::LoopInfo> CustomAudioPlayer::inspectLoopInfo(const std::filesystem::path &path)
{
	std::string ext = toLowerAscii(path.extension().string());
	if (ext == ".ogg")
	{
		FILE *f = _wfopen(path.c_str(), L"rb");
		if (!f)
		{
			return std::nullopt;
		}
		int err = 0;
		stb_vorbis *v = stb_vorbis_open_file(f, FALSE, &err, nullptr);
		if (!v)
		{
			std::fclose(f);
			return std::nullopt;
		}
		LoopInfo info = extractOggLoopInfo(v);
		stb_vorbis_close(v);
		std::fclose(f);
		return info;
	}
	else if (ext == ".wav")
	{
		return extractWavSmplLoopInfo(path);
	}
	return std::nullopt;
}

bool CustomAudioPlayer::play(const std::filesystem::path &filePath, bool loop, std::size_t playerSlot)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (!m_initialized && !init())
	{
		return false;
	}

	if (playerSlot >= m_slots.size())
	{
		m_slots.resize(playerSlot + 1);
		for (auto &s : m_slots)
		{
			if (!s)
				s = std::make_unique<SlotState>();
		}
	}

	SlotState &slot = *m_slots[playerSlot];
	slot.cleanup();

	std::filesystem::path resolvedPath;
	int subTrackIndex = 0;
	resolvePathAndSubTrack(filePath, resolvedPath, subTrackIndex);

	std::string ext = toLowerAscii(resolvedPath.extension().string());
	ma_data_source *dataSourcePtr = nullptr;
	LoopInfo loopInfo{};

	if (ext == ".ogg")
	{
		auto ogg = std::make_unique<OggLoopDataSource>();
		if (!ogg->open(resolvedPath, loop))
		{
			return false;
		}
		loopInfo = ogg->loopInfo;
		dataSourcePtr = &ogg->base;
		slot.oggSource = std::move(ogg);
	}
	else if (isGmeExtension(ext))
	{
		auto gme = std::make_unique<GmeDataSource>();
		std::uint32_t engineSampleRate = ma_engine_get_sample_rate(m_engine);
		if (!gme->open(resolvedPath, subTrackIndex, loop, engineSampleRate > 0 ? engineSampleRate : 44100))
		{
			return false;
		}
		loopInfo.hasLoopTag = loop;
		loopInfo.sampleRate = gme->sampleRate;
		dataSourcePtr = &gme->base;
		slot.gmeSource = std::move(gme);
	}
	else if (isStandardExtension(ext))
	{
		auto dec = std::make_unique<ma_decoder>();
		if (ma_decoder_init_file_w(resolvedPath.c_str(), nullptr, dec.get()) != MA_SUCCESS)
		{
			return false;
		}

		ma_uint64 totalLen = 0;
		ma_decoder_get_length_in_pcm_frames(dec.get(), &totalLen);
		loopInfo.totalFrames = totalLen;
		loopInfo.sampleRate = dec->outputSampleRate;

		if (ext == ".wav")
		{
			if (auto wavLoop = extractWavSmplLoopInfo(resolvedPath); wavLoop.has_value() && wavLoop->hasLoopTag)
			{
				loopInfo = *wavLoop;
				ma_data_source_set_loop_point_in_pcm_frames(dec.get(), loopInfo.loopStartFrame, loopInfo.loopEndFrame);
			}
		}

		ma_data_source_set_looping(dec.get(), loop ? MA_TRUE : MA_FALSE);
		dataSourcePtr = dec.get();
		slot.stdDecoder = std::move(dec);
	}
	else
	{
		return false;
	}

	auto snd = std::make_unique<ma_sound>();
	if (ma_sound_init_from_data_source(m_engine, dataSourcePtr, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, snd.get()) != MA_SUCCESS)
	{
		slot.cleanup();
		return false;
	}

	ma_sound_set_volume(snd.get(), getFormatGain(ext));
	ma_sound_set_looping(snd.get(), loop ? MA_TRUE : MA_FALSE);
	if (ma_sound_start(snd.get()) != MA_SUCCESS)
	{
		slot.cleanup();
		return false;
	}

	slot.sound = std::move(snd);
	slot.active = true;
	slot.paused = false;
	slot.fadingOut = false;
	slot.metadata = TrackMetadata{
		.filePath = resolvedPath,
		.format = ext,
		.subTrackIndex = subTrackIndex,
		.loopInfo = loopInfo,
	};

	return true;
}

void CustomAudioPlayer::stop(std::size_t playerSlot)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (playerSlot < m_slots.size() && m_slots[playerSlot])
	{
		m_slots[playerSlot]->cleanup();
	}
}

void CustomAudioPlayer::stopAll()
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	for (auto &slot : m_slots)
	{
		if (slot)
		{
			slot->cleanup();
		}
	}
}

void CustomAudioPlayer::fadeOutAndStop(std::uint32_t durationMs, std::size_t playerSlot)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (playerSlot >= m_slots.size() || !m_slots[playerSlot] || !m_slots[playerSlot]->active || !m_slots[playerSlot]->sound)
	{
		return;
	}

	if (durationMs == 0)
	{
		m_slots[playerSlot]->cleanup();
		return;
	}

	SlotState &slot = *m_slots[playerSlot];
	ma_sound_set_fade_in_milliseconds(slot.sound.get(), -1.0f, 0.0f, durationMs);
	slot.fadingOut = true;
	slot.fadeEndTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
}

void CustomAudioPlayer::pause(std::size_t playerSlot)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (playerSlot < m_slots.size() && m_slots[playerSlot] && m_slots[playerSlot]->active && m_slots[playerSlot]->sound)
	{
		ma_sound_stop(m_slots[playerSlot]->sound.get());
		m_slots[playerSlot]->paused = true;
	}
}

void CustomAudioPlayer::resume(std::size_t playerSlot)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (playerSlot < m_slots.size() && m_slots[playerSlot] && m_slots[playerSlot]->active && m_slots[playerSlot]->sound && m_slots[playerSlot]->paused)
	{
		ma_sound_start(m_slots[playerSlot]->sound.get());
		m_slots[playerSlot]->paused = false;
	}
}

bool CustomAudioPlayer::isPlaying(std::size_t playerSlot) const
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (playerSlot >= m_slots.size() || !m_slots[playerSlot] || !m_slots[playerSlot]->active || !m_slots[playerSlot]->sound)
	{
		return false;
	}
	if (m_slots[playerSlot]->paused)
	{
		return false;
	}
	return ma_sound_is_playing(m_slots[playerSlot]->sound.get()) == MA_TRUE;
}

bool CustomAudioPlayer::isPaused(std::size_t playerSlot) const
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (playerSlot >= m_slots.size() || !m_slots[playerSlot] || !m_slots[playerSlot]->active)
	{
		return false;
	}
	return m_slots[playerSlot]->paused;
}

void CustomAudioPlayer::setMasterVolume(float volume)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
	if (m_engine)
	{
		ma_engine_set_volume(m_engine, m_masterVolume);
	}
}

float CustomAudioPlayer::getMasterVolume() const
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	return m_masterVolume;
}

void CustomAudioPlayer::update()
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	auto now = std::chrono::steady_clock::now();
	for (auto &slot : m_slots)
	{
		if (!slot || !slot->active)
		{
			continue;
		}
		if (slot->fadingOut && now >= slot->fadeEndTime)
		{
			slot->cleanup();
		}
		else if (!slot->paused && slot->sound && ma_sound_at_end(slot->sound.get()) == MA_TRUE)
		{
			slot->cleanup();
		}
	}
}

std::optional<CustomAudioPlayer::TrackMetadata> CustomAudioPlayer::getCurrentTrack(std::size_t playerSlot) const
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);
	if (playerSlot >= m_slots.size() || !m_slots[playerSlot] || !m_slots[playerSlot]->active)
	{
		return std::nullopt;
	}
	return m_slots[playerSlot]->metadata;
}
