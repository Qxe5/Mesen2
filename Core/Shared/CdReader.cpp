#include "stdafx.h"
#include "CdReader.h"
#include "Shared/MessageManager.h"
#include "Utilities/StringUtilities.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/magic_enum.hpp"

struct CueIndexEntry
{
	uint32_t Number;
	DiscPosition Position;
};

struct CueGapEntry
{
	bool HasGap = false;
	DiscPosition Length;
};

struct CueTrackEntry
{
	uint32_t Number;
	string Format;
	CueGapEntry PreGap = {};
	vector<CueIndexEntry> Indexes;
};

struct CueFileEntry
{
	string Filename;
	vector<CueTrackEntry> Tracks;
};

bool CdReader::LoadCue(VirtualFile& cueFile, DiscInfo& disc)
{
	vector<CueFileEntry> files;

	stringstream ss;
	cueFile.ReadFile(ss);

	string line;
	while(std::getline(ss, line)) {
		line = StringUtilities::TrimLeft(StringUtilities::TrimRight(line));

		if(line.substr(0, 4) == string("FILE")) {
			size_t start = line.find_first_of('"');
			size_t end = line.find_last_of('"');
			if(start != end && start != string::npos && end != string::npos) {
				string filename = line.substr(start + 1, end - start - 1);

				VirtualFile dataFile = cueFile.GetFolderPath() + filename;
				if(cueFile.IsArchive()) {
					dataFile = VirtualFile(cueFile.GetFilePath(), filename);
				}
				files.push_back({ dataFile });
			}
		} else if(line.substr(0, 5) == string("TRACK")) {
			if(files.size() == 0) {
				MessageManager::Log("[CUE] Unexcepted TRACK entry");
				return false;
			}

			vector<string> entry = StringUtilities::Split(line, ' ');

			if(entry.size() < 3) {
				MessageManager::Log("[CUE] Invalid TRACK entry");
				return false;
			}

			CueTrackEntry trk = {};
			try {
				trk.Number = std::stoi(entry[1]);
			} catch(const std::exception&) {
				MessageManager::Log("[CUE] Invalid TRACK number");
				return false;
			}

			trk.Format = entry[2];
			files[files.size() - 1].Tracks.push_back(trk);
		} else if(line.substr(0, 6) == string("PREGAP")) {
			if(files.empty() || files[files.size() - 1].Tracks.empty()) {
				MessageManager::Log("[CUE] Unexcepted PREGAP entry");
				return false;
			}
			
			vector<string> entry = StringUtilities::Split(line, ' ');
			
			CueGapEntry gap = {};

			vector<string> lengthParts = StringUtilities::Split(entry[1], ':');
			if(lengthParts.size() != 3) {
				MessageManager::Log("[CUE] Invalid PREGAP time format");
				return false;
			}

			try {
				gap.Length.Minutes = std::stoi(lengthParts[0]);
				gap.Length.Seconds = std::stoi(lengthParts[1]);
				gap.Length.Frames = std::stoi(lengthParts[2]);
				gap.HasGap = true;
			} catch(const std::exception&) {
				MessageManager::Log("[CUE] Invalid PREGAP time format");
				return false;
			}

			files[files.size() - 1].Tracks[files[files.size() - 1].Tracks.size() - 1].PreGap = gap;
		} else if(line.substr(0, 5) == string("INDEX")) {
			if(files.empty() || files[files.size() - 1].Tracks.empty()) {
				MessageManager::Log("[CUE] Unexcepted INDEX entry");
				return false;
			}

			vector<string> entry = StringUtilities::Split(line, ' ');
			CueIndexEntry idx = {};
			try {
				idx.Number = std::stoi(entry[1]);
			} catch(const std::exception&) {
				MessageManager::Log("[CUE] Invalid INDEX number");
				return false;
			}

			vector<string> lengthParts = StringUtilities::Split(entry[2], ':');
			if(lengthParts.size() != 3) {
				MessageManager::Log("[CUE] Invalid INDEX time format");
				return false;
			}

			try {
				idx.Position.Minutes = std::stoi(lengthParts[0]);
				idx.Position.Seconds = std::stoi(lengthParts[1]);
				idx.Position.Frames = std::stoi(lengthParts[2]);
			} catch(const std::exception&) {
				MessageManager::Log("[CUE] Invalid INDEX time format");
				return false;
			}

			files[files.size() - 1].Tracks[files[files.size() - 1].Tracks.size() - 1].Indexes.push_back(idx);
		}
	}

	uint32_t discSize = 0;
	uint32_t totalPregapLbaLength = 0;
	for(size_t i = 0; i < files.size(); i++) {
		VirtualFile physicalFile = files[i].Filename;
		if(!physicalFile.IsValid()) {
			MessageManager::Log("[CUE] Missing or invalid file: " + files[i].Filename);
			return false;
		}

		disc.Files.push_back(files[i].Filename);

		for(size_t j = 0; j < files[i].Tracks.size(); j++) {
			CueTrackEntry entry = files[i].Tracks[j];
			TrackInfo trk = {};

			if(entry.PreGap.HasGap) {
				totalPregapLbaLength += entry.PreGap.Length.ToLba();
			}

			DiscPosition startPos;
			for(CueIndexEntry& idx : entry.Indexes) {
				if(idx.Number == 0) {
					trk.HasLeadIn = true;
					trk.LeadInPosition = DiscPosition::FromLba(idx.Position.ToLba() + discSize / DiscInfo::SectorSize);
				} else if(idx.Number == 1) {
					if(entry.PreGap.HasGap) {
						trk.HasLeadIn = true;
						trk.LeadInPosition = DiscPosition::FromLba(idx.Position.ToLba() + totalPregapLbaLength - entry.PreGap.Length.ToLba() + discSize / DiscInfo::SectorSize);
					}
					trk.StartPosition = DiscPosition::FromLba(idx.Position.ToLba() + totalPregapLbaLength + discSize / DiscInfo::SectorSize);
					startPos = idx.Position;
				}
			}

			if(entry.Format == "AUDIO") {
				trk.Format = TrackFormat::Audio;
			} else if(entry.Format == "MODE1/2352") {
				trk.Format = TrackFormat::Mode1_2352;
			} else {
				MessageManager::Log("[CUE] Unsupported track format: " + entry.Format);
				return false;
			}

			trk.FirstSector = trk.StartPosition.ToLba();
			trk.FileOffset = startPos.ToLba() * DiscInfo::SectorSize;
			trk.FileIndex = (uint32_t)disc.Files.size() - 1;

			if(disc.Tracks.size() > 0) {
				TrackInfo& prvTrk = disc.Tracks[disc.Tracks.size() - 1];
				prvTrk.EndPosition = DiscPosition::FromLba((trk.HasLeadIn ? trk.LeadInPosition.ToLba() : trk.FirstSector) - 1);
				prvTrk.LastSector = prvTrk.EndPosition.ToLba();
				prvTrk.SectorCount = prvTrk.LastSector - prvTrk.FirstSector + 1;
				prvTrk.Size = prvTrk.SectorCount * DiscInfo::SectorSize;
			}

			disc.Tracks.push_back(trk);
		}

		discSize += (uint32_t)physicalFile.GetSize();
	}

	TrackInfo& lastTrk = disc.Tracks[disc.Tracks.size() - 1];
	lastTrk.Size = (disc.Files[lastTrk.FileIndex].GetSize() - lastTrk.FileOffset) / DiscInfo::SectorSize * DiscInfo::SectorSize;
	lastTrk.SectorCount = lastTrk.Size / DiscInfo::SectorSize;
	lastTrk.EndPosition = DiscPosition::FromLba(lastTrk.FirstSector+lastTrk.SectorCount-1);
	lastTrk.LastSector = lastTrk.EndPosition.ToLba();

	disc.EndPosition = lastTrk.EndPosition;
	disc.DiscSize = discSize;
	disc.DiscSectorCount = discSize / DiscInfo::SectorSize;

	MessageManager::Log("---- DISC TRACKS ----");
	int i = 1;
	for(TrackInfo& trk : disc.Tracks) {
		MessageManager::Log("Track " + std::to_string(i) + " (" + string(magic_enum::enum_name(trk.Format)) + ")");
		if(trk.HasLeadIn) {
			MessageManager::Log("  Lead-in: " + trk.LeadInPosition.ToString());
		}
		MessageManager::Log("  Time: " + trk.StartPosition.ToString() + " - " + trk.EndPosition.ToString());
		MessageManager::Log("  Sectors: " + std::to_string(trk.FirstSector) + " - " + std::to_string(trk.LastSector));
		MessageManager::Log("  File offset: " + std::to_string(trk.FileOffset) + " - " + std::to_string(trk.FileOffset+trk.Size-1));
		i++;
	}
	MessageManager::Log("---- END TRACKS ----");

	return true;
}
