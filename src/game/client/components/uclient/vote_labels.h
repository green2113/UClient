#ifndef GAME_CLIENT_COMPONENTS_UCLIENT_VOTE_LABELS_H
#define GAME_CLIENT_COMPONENTS_UCLIENT_VOTE_LABELS_H

#include <game/client/component.h>

#include <string>
#include <vector>

// Display names for server vote options. The original description is still
// what gets sent when the vote is called.
class CVoteLabels : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;

	const char *Display(const char *pOriginal) const;
	bool Has(const char *pOriginal) const;
	// Saves a trimmed label of at least one character. Returns false and keeps
	// the previous label when the text is shorter than that.
	bool Set(const char *pOriginal, const char *pLabel);
	void Clear(const char *pOriginal);

private:
	struct SLabel
	{
		std::string m_From;
		std::string m_To;
	};

	void Load();
	void Save() const;
	void Upsert(const char *pOriginal, const char *pLabel);
	std::vector<SLabel>::iterator Find(const char *pOriginal);
	std::vector<SLabel>::const_iterator Find(const char *pOriginal) const;

	std::vector<SLabel> m_vLabels;
};

#endif
