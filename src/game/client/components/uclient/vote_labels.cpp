#include "vote_labels.h"

#include <base/str.h>
#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <game/voting.h>

#include <algorithm>

static constexpr const char *VOTE_LABELS_FILE = "uclient_vote_labels.json";

static bool NormalizeLabel(const char *pLabel, char *pOut, int OutSize)
{
	if(!pLabel || OutSize <= 1)
		return false;
	str_copy(pOut, pLabel, OutSize);
	str_utf8_trim_right(pOut);
	const char *pStart = str_utf8_skip_whitespaces(pOut);
	if(pStart != pOut)
	{
		char aMoved[VOTE_DESC_LENGTH];
		str_copy(aMoved, pStart);
		str_copy(pOut, aMoved, OutSize);
	}
	size_t ByteSize = 0;
	size_t CharCount = 0;
	str_utf8_stats(pOut, (size_t)OutSize, (size_t)-1, &ByteSize, &CharCount);
	(void)ByteSize;
	return CharCount >= 1;
}

void CVoteLabels::OnInit()
{
	Load();
}

const char *CVoteLabels::Display(const char *pOriginal) const
{
	if(!pOriginal)
		return "";
	const auto It = Find(pOriginal);
	if(It == m_vLabels.end() || str_comp(It->m_To.c_str(), pOriginal) == 0)
		return pOriginal;
	return It->m_To.c_str();
}

bool CVoteLabels::Has(const char *pOriginal) const
{
	if(!pOriginal)
		return false;
	const auto It = Find(pOriginal);
	return It != m_vLabels.end() && str_comp(It->m_To.c_str(), pOriginal) != 0;
}

bool CVoteLabels::Set(const char *pOriginal, const char *pLabel)
{
	if(!pOriginal || pOriginal[0] == '\0')
		return false;
	char aLabel[VOTE_DESC_LENGTH];
	if(!NormalizeLabel(pLabel, aLabel, sizeof(aLabel)))
		return false;
	char aOriginal[VOTE_DESC_LENGTH];
	str_copy(aOriginal, pOriginal);
	if(str_comp(aOriginal, aLabel) == 0)
	{
		Clear(aOriginal);
		return true;
	}
	Upsert(aOriginal, aLabel);
	Save();
	return true;
}

void CVoteLabels::Clear(const char *pOriginal)
{
	if(!pOriginal)
		return;
	const auto End = std::remove_if(m_vLabels.begin(), m_vLabels.end(), [pOriginal](const SLabel &Label) {
		return str_comp(Label.m_From.c_str(), pOriginal) == 0;
	});
	if(End == m_vLabels.end())
		return;
	m_vLabels.erase(End, m_vLabels.end());
	Save();
}

void CVoteLabels::Upsert(const char *pOriginal, const char *pLabel)
{
	const auto It = Find(pOriginal);
	if(It != m_vLabels.end())
	{
		It->m_To = pLabel;
		return;
	}
	m_vLabels.push_back({pOriginal, pLabel});
}

std::vector<CVoteLabels::SLabel>::iterator CVoteLabels::Find(const char *pOriginal)
{
	return std::find_if(m_vLabels.begin(), m_vLabels.end(), [pOriginal](const SLabel &Label) {
		return str_comp(Label.m_From.c_str(), pOriginal) == 0;
	});
}

std::vector<CVoteLabels::SLabel>::const_iterator CVoteLabels::Find(const char *pOriginal) const
{
	return std::find_if(m_vLabels.begin(), m_vLabels.end(), [pOriginal](const SLabel &Label) {
		return str_comp(Label.m_From.c_str(), pOriginal) == 0;
	});
}

void CVoteLabels::Load()
{
	m_vLabels.clear();
	void *pData = nullptr;
	unsigned Length = 0;
	if(!Storage()->ReadFile(VOTE_LABELS_FILE, IStorage::TYPE_SAVE, &pData, &Length) || !pData)
		return;
	json_settings JsonSettings{};
	char aError[256];
	json_value *pRoot = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pData), Length, aError);
	free(pData);
	if(!pRoot)
	{
		(void)aError;
		return;
	}
	if(pRoot->type == json_array)
	{
		const int Count = json_array_length(pRoot);
		for(int i = 0; i < Count && (int)m_vLabels.size() < MAX_VOTE_OPTIONS; i++)
		{
			const json_value *pItem = json_array_get(pRoot, i);
			if(!pItem || pItem->type != json_object)
				continue;
			const json_value *pFrom = json_object_get(pItem, "from");
			const json_value *pTo = json_object_get(pItem, "to");
			if(!pFrom || pFrom->type != json_string || !pTo || pTo->type != json_string)
				continue;
			char aFrom[VOTE_DESC_LENGTH];
			char aTo[VOTE_DESC_LENGTH];
			str_copy(aFrom, json_string_get(pFrom));
			if(aFrom[0] == '\0' || !NormalizeLabel(json_string_get(pTo), aTo, sizeof(aTo)) || str_comp(aFrom, aTo) == 0)
				continue;
			Upsert(aFrom, aTo);
		}
	}
	json_value_free(pRoot);
}

void CVoteLabels::Save() const
{
	IOHANDLE File = Storage()->OpenFile(VOTE_LABELS_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	CJsonFileWriter Writer(File);
	Writer.BeginArray();
	for(const SLabel &Label : m_vLabels)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("from");
		Writer.WriteStrValue(Label.m_From.c_str());
		Writer.WriteAttribute("to");
		Writer.WriteStrValue(Label.m_To.c_str());
		Writer.EndObject();
	}
	Writer.EndArray();
}
