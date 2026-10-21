/*
COOP_NAME_TEST.C

xbox_coop_name (port/xbox/src/xbox_coop_name.c): a co-op host's variant name,
with the difficulty other builds add, and nothing that only starts like it.
*/

#include <stdio.h>
#include <wchar.h>

int xbox_coop_name(wchar_t const *description);

static int failures;

static void expect(int result, wchar_t const *name, int wanted)
{
	if ((result != 0) != wanted)
	{
		printf("FAIL: \"%ls\" should %sbe co-op\n", name ? name : L"(null)", wanted ? "" : "not ");
		failures++;
	}
}

int main(void)
{
	expect(xbox_coop_name(L"Co-op"), L"Co-op", 1);
	expect(xbox_coop_name(L"Co-op Heroic"), L"Co-op Heroic", 1);
	expect(xbox_coop_name(L"Co-op Legendary"), L"Co-op Legendary", 1);
	expect(xbox_coop_name(L"Co-operative"), L"Co-operative", 0);
	expect(xbox_coop_name(L"Co-opx"), L"Co-opx", 0);
	expect(xbox_coop_name(L"co-op"), L"co-op", 0);
	expect(xbox_coop_name(L"Slayer"), L"Slayer", 0);
	expect(xbox_coop_name(L""), L"", 0);
	expect(xbox_coop_name(L"Co"), L"Co", 0);
	expect(xbox_coop_name(NULL), NULL, 0);
	if (failures)
		return 1;
	printf("coop_name_test: ok\n");
	return 0;
}
