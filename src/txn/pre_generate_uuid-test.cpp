#include <stdlib.h>
#include <string.h>
#include <set>

#include <gtest/gtest.h>
#include "pre_generate_uuid.h"

TEST(PreGenerateTest, SimplePassGuarded4) {

	open_claim4 cla;
	cla.claim = CLAIM_NULL;
	OPEN4args arg_OPEN4;
	arg_OPEN4.claim = cla;
	openflag4 ol;
	ol.opentype = OPEN4_CREATE;
	createhow4 c;
	c.mode = GUARDED4;
	ol.openflag4_u.how = c;
	arg_OPEN4.openhow = ol;
	EXPECT_EQ(1, pre_generate_open_flag_check(&arg_OPEN4));
}
TEST(PreGenerateTest, SimplePassUnchecked4) {

	open_claim4 cla;
	cla.claim = CLAIM_NULL;
	OPEN4args arg_OPEN4;
	arg_OPEN4.claim = cla;
	openflag4 ol;
	ol.opentype = OPEN4_CREATE;
	createhow4 c;
	c.mode = UNCHECKED4;
	ol.openflag4_u.how = c;
	arg_OPEN4.openhow = ol;
	EXPECT_EQ(1, pre_generate_open_flag_check(&arg_OPEN4));
}
TEST(PreGenerateTest, SimpleFailCLAIM_PREVIOUS) {

	open_claim4 cla;
	cla.claim = CLAIM_PREVIOUS;
	OPEN4args arg_OPEN4;
	arg_OPEN4.claim = cla;
	openflag4 ol;
	ol.opentype = OPEN4_CREATE;
	createhow4 c;
	c.mode = UNCHECKED4;
	ol.openflag4_u.how = c;
	arg_OPEN4.openhow = ol;
	EXPECT_EQ(0, pre_generate_open_flag_check(&arg_OPEN4));
}
TEST(PreGenerateTest, SimpleFailOPEN4_NOCREATE) {

	open_claim4 cla;
	cla.claim = CLAIM_NULL;
	OPEN4args arg_OPEN4;
	arg_OPEN4.claim = cla;
	openflag4 ol;
	ol.opentype = OPEN4_NOCREATE;
	createhow4 c;
	c.mode = EXCLUSIVE4;
	ol.openflag4_u.how = c;
	arg_OPEN4.openhow = ol;
	EXPECT_EQ(0, pre_generate_open_flag_check(&arg_OPEN4));
}
TEST(PreGenerateTest, SimpleFailEXCLUSIVE4) {

	open_claim4 cla;
	cla.claim = CLAIM_NULL;
	OPEN4args arg_OPEN4;
	arg_OPEN4.claim = cla;
	openflag4 ol;
	ol.opentype = OPEN4_CREATE;
	createhow4 c;
	c.mode = EXCLUSIVE4;
	ol.openflag4_u.how = c;
	arg_OPEN4.openhow = ol;
	EXPECT_EQ(0, pre_generate_open_flag_check(&arg_OPEN4));
}

int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
