/* Copyright (c) 2018 Pigeonhole authors, see the included COPYING file */

#include "lib.h"
#include "test-common.h"
#include "str.h"

#include "rfc2822.h"

struct test_header_write {
	const char *name;
	const char *body;
	const char *output;
};

static const struct test_header_write header_write_tests[] = {
	{
		.name = "Frop",
		.body = "Bladiebla",
		.output = "Frop: Bladiebla\r\n"
	},{
		.name = "Subject",
		.body = "This is a very long subject that well exceeds the "
			"boundary of 80 characters. It should therefore "
			"trigger the header folding algorithm.",
		.output =
			"Subject: This is a very long subject that well "
			"exceeds the boundary of 80\r\n"
			"\tcharacters. It should therefore trigger the header "
			"folding algorithm.\r\n"
	},{
		.name = "Subject",
		.body = "This\tis\ta\tvery\tlong\tsubject\tthat\twell\texceeds"
			"\tthe\tboundary\tof\t80\tcharacters.\tIt\tshould\t"
			"therefore\ttrigger\tthe\theader\tfolding\talgorithm.",
		.output =
			"Subject: This\tis\ta\tvery\tlong\tsubject\tthat\twell"
			"\texceeds\tthe\tboundary\tof\t80\r\n"
			"\tcharacters.\tIt\tshould\ttherefore\ttrigger\tthe\t"
			"header\tfolding\talgorithm.\r\n"
	},{
		.name = "Comment",
		.body = "This header already contains newlines.\n"
			"The header folding algorithm should respect these.\n"
			"It also should convert between CRLF and LF when "
			"needed.",
		.output = "Comment: This header already contains newlines.\r\n"
			"\tThe header folding algorithm should respect "
			"these.\r\n"
			"\tIt also should convert between CRLF and LF when "
			"needed.\r\n"
	},{
		.name = "References",
		.body = "<messageid1@example.com> <messageid2@example.com> "
			"<extremelylonglonglonglonglonglonglonglonglonglong"
			"longlongmessageid3@example.com> "
			"<messageid4@example.com>",
		.output = "References: <messageid1@example.com> "
			"<messageid2@example.com>\r\n"
			"\t<extremelylonglonglonglonglonglonglonglonglonglong"
			"longlongmessageid3@example.com>\r\n"
			"\t<messageid4@example.com>\r\n",
	},{
		.name = "Cc",
		.body = "\"Richard Edgar Cipient\"   "
			"<r.e.cipient@example.com>,   \"Albert Buser\"   "
			"<a.buser@example.com>,   \"Steven Pammer\"  "
			"<s.pammer@example.com>",
		.output = "Cc: \"Richard Edgar Cipient\"   "
			"<r.e.cipient@example.com>,   \"Albert Buser\"\r\n"
			"\t<a.buser@example.com>,   \"Steven Pammer\"  "
			"<s.pammer@example.com>\r\n"
	},{
		.name = "References",
		.body = "<00fd01d31b6c$33d98e30$9b8caa90$@karel@aa.example.org"
			"> <00f201d31c36$afbfa320$0f3ee960$@karel@aa.example.o"
			"rg>   <015c01d32023$fe3840c0$faa8c240$@karel@aa.examp"
			"le.org>    <014601d325a4$ece1ed90$c6a5c8b0$@karel@aa."
			"example.org>   <012801d32b24$7734c380$659e4a80$@karel"
			"@aa.example.org> <00da01d32be9$2d9944b0$88cbce10$@kar"
			"el@aa.example.org>      <006a01d336ef$6825d5b0$387181"
			"10$@karel@aa.example.org>  <018501d33880$58b654f0$0a2"
			"2fed0$@frederik@aa.example.org> <00e601d33ba3$be50f10"
			"0$3af2d300$@frederik@aa.example.org>  <016501d341ee$e"
			"678e1a0$b36aa4e0$@frederik@aa.example.org>    <00ab01"
			"d348f9$ae2e1ab0$0a8a5010$@karel@aa.example.org> <0086"
			"01d349c1$98ff4ba0$cafde2e0$@frederik@aa.example.org> "
			" <019301d357e6$a2190680$e64b1380$@frederik@aa.example"
			".org>                             <025f01d384b0$24d2c"
			"660$6e785320$@karel@aa.example.org>   <01cf01d3889e$7"
			"280cb90$578262b0$@karel@aa.example.org>    <013701d38"
			"bc2$9164b950$b42e2bf0$@karel@aa.example.org>         "
			"       <014f01d3a5b1$a51afc80$ef\n"
			"               \n"
			"\t \t \t \t \t \t \t \t5\t0\tf\t5\t8\t0\t$\t@\tk\ta\t"
			"r\te\tl\t@\taa.example.org>        <01cb01d3af29$dd7d"
			"1b40$987751c0$@karel@aa.example.org>                 "
			"                      <00b401d3f2bc$6ad8c180$408a4480"
			"$@karel@aa.example.org>   <011a01d3f6ab$0eeb0480$2cc1"
			"0d80$@frederik@aa.example.org> <005c01d3f774$37f1b210"
			"$a7d51630$@richard@aa.example.org>   <01a801d3fc2d$59"
			"0f7730$0b2e6590$@frederik@aa.example.org> <007501d3fc"
			"f5$23d75ce0$6b8616a0$@frederik@aa.example.org> <015d0"
			"1d3fdbf$136da510$3a48ef30$@frederik@aa.example.org> <"
			"021a01d3fe87$556d68b0$00483a10$@frederik@aa.example.o"
			"rg> <013f01d3ff4e$a2d13d30$e873b790$@frederik@aa.exam"
			"ple.org> <001f01d401ab$31e7b090$95b711b0$@frederik@aa"
			".example.org> <017201d40273$a118d200$e34a7600$@freder"
			"ik@aa.example.org> <017401d4033e$ca3602e0$5ea208a0$@f"
			"rederik@aa.example.org> <02a601d40404$608b9e10$21a2da"
			"30$@frederik@aa.example.org> <014301d404d0$b65269b0$2"
			"2f73d10$@frederik@aa.example.org> <015901d4072b$b5a1b"
			"950$20e52bf0$@frederik@aa.example.org> <01b401d407f3$"
			"bef52050$3cdf\n"
			" 60 \n"
			"\tf0 \t$@ \tfr \ted \teri\tk@aa.example.org> <012801d"
			"408bd$6602fce0$3208f6a0$@frederik@aa.example.org> <01"
			"c801d40984$ae4b23c0$0ae16b40$@frederik@aa.example.org"
			"> <00ec01d40a4d$12859190$3790b4b0$@frederik@aa.exampl"
			"e.org> <02af01d40d74$589c9050$09d5b0f0$@frederik@aa.e"
			"xample.org> <000d01d40ec8$d3d337b0$7b79a710$@richard@"
			"aa.example.org>\n",
		.output = "References: <00fd01d31b6c$33d98e30$9b8caa90$@karel@aa.example.org>\r\n"
			"\t<00f201d31c36$afbfa320$0f3ee960$@karel@aa.example.org>\r\n"
			"\t<015c01d32023$fe3840c0$faa8c240$@karel@aa.example.org>\r\n"
			"\t<014601d325a4$ece1ed90$c6a5c8b0$@karel@aa.example.org>\r\n"
			"\t<012801d32b24$7734c380$659e4a80$@karel@aa.example.org>\r\n"
			"\t<00da01d32be9$2d9944b0$88cbce10$@karel@aa.example.org>\r\n"
			"\t<006a01d336ef$6825d5b0$38718110$@karel@aa.example.org>\r\n"
			"\t<018501d33880$58b654f0$0a22fed0$@frederik@aa.example.org>\r\n"
			"\t<00e601d33ba3$be50f100$3af2d300$@frederik@aa.example.org>\r\n"
			"\t<016501d341ee$e678e1a0$b36aa4e0$@frederik@aa.example.org>\r\n"
			"\t<00ab01d348f9$ae2e1ab0$0a8a5010$@karel@aa.example.org>\r\n"
			"\t<008601d349c1$98ff4ba0$cafde2e0$@frederik@aa.example.org>\r\n"
			"\t<019301d357e6$a2190680$e64b1380$@frederik@aa.example.org>\r\n"
			"\t<025f01d384b0$24d2c660$6e785320$@karel@aa.example.org>\r\n"
			"\t<01cf01d3889e$7280cb90$578262b0$@karel@aa.example.org>\r\n"
			"\t<013701d38bc2$9164b950$b42e2bf0$@karel@aa.example.org>\r\n"
			"\t<014f01d3a5b1$a51afc80$ef\r\n"
			"\t5\t0\tf\t5\t8\t0\t$\t@\tk\ta\tr\te\tl\t@\taa.example.org>\r\n"
			"\t<01cb01d3af29$dd7d1b40$987751c0$@karel@aa.example.org>\r\n"
			"\t<00b401d3f2bc$6ad8c180$408a4480$@karel@aa.example.org>\r\n"
			"\t<011a01d3f6ab$0eeb0480$2cc10d80$@frederik@aa.example.org>\r\n"
			"\t<005c01d3f774$37f1b210$a7d51630$@richard@aa.example.org>\r\n"
			"\t<01a801d3fc2d$590f7730$0b2e6590$@frederik@aa.example.org>\r\n"
			"\t<007501d3fcf5$23d75ce0$6b8616a0$@frederik@aa.example.org>\r\n"
			"\t<015d01d3fdbf$136da510$3a48ef30$@frederik@aa.example.org>\r\n"
			"\t<021a01d3fe87$556d68b0$00483a10$@frederik@aa.example.org>\r\n"
			"\t<013f01d3ff4e$a2d13d30$e873b790$@frederik@aa.example.org>\r\n"
			"\t<001f01d401ab$31e7b090$95b711b0$@frederik@aa.example.org>\r\n"
			"\t<017201d40273$a118d200$e34a7600$@frederik@aa.example.org>\r\n"
			"\t<017401d4033e$ca3602e0$5ea208a0$@frederik@aa.example.org>\r\n"
			"\t<02a601d40404$608b9e10$21a2da30$@frederik@aa.example.org>\r\n"
			"\t<014301d404d0$b65269b0$22f73d10$@frederik@aa.example.org>\r\n"
			"\t<015901d4072b$b5a1b950$20e52bf0$@frederik@aa.example.org>\r\n"
			"\t<01b401d407f3$bef52050$3cdf\r\n"
			"\t60\r\n"
			"\tf0 \t$@ \tfr \ted \teri\tk@aa.example.org>\r\n"
			"\t<012801d408bd$6602fce0$3208f6a0$@frederik@aa.example.org>\r\n"
			"\t<01c801d40984$ae4b23c0$0ae16b40$@frederik@aa.example.org>\r\n"
			"\t<00ec01d40a4d$12859190$3790b4b0$@frederik@aa.example.org>\r\n"
			"\t<02af01d40d74$589c9050$09d5b0f0$@frederik@aa.example.org>\r\n"
			"\t<000d01d40ec8$d3d337b0$7b79a710$@richard@aa.example.org>\r\n"
	}
};

static const unsigned int header_write_tests_count =
	N_ELEMENTS(header_write_tests);

static void test_rfc2822_header_write(void)
{
	string_t *header;
	unsigned int i;

	test_begin("rfc2822 - header write");

	header = t_str_new(1024);
	for (i = 0; i < header_write_tests_count; i++) {
		const struct test_header_write *test = &header_write_tests[i];

		str_truncate(header, 0);
		rfc2822_header_write(header, test->name, test->body);

		test_assert_idx(strcmp(str_c(header), test->output) == 0, i);
	}

	test_end();
}

int main(void)
{
	static void (*test_functions[])(void) = {
		test_rfc2822_header_write,
		NULL
	};
	return test_run(test_functions);
}

