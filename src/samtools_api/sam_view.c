#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "sam.h"

static int g_min_mapQ = 0, g_flag_on = 0, g_flag_off = 0;

#define __g_skip_aln(b) (((b)->core.qual < g_min_mapQ) || ((b->core.flag & g_flag_on) != g_flag_on) \
						 || (b->core.flag & g_flag_off))

// callback function for bam_fetch()
static int view_func(const bam1_t *b, void *data)
{
	if (!__g_skip_aln(b)) samwrite((samfile_t*)data, b);
	return 0;
}

static int usage(void);

int main_samview(int argc, char *argv[])
{
	int c, is_header = 0, is_header_only = 0, is_bamin = 1, ret = 0;
	samfile_t *in = 0, *out = 0;
	char in_mode[4], out_mode[4], *fn_out = 0, *fn_list = 0;

	/* parse command-line options */
	strcpy(in_mode, "r"); strcpy(out_mode, "w");
	while ((c = getopt(argc, argv, "Sbt:hHo:q:f:F:")) >= 0) {
		switch (c) {
		case 'S': is_bamin = 0; break;
		case 'b': strcat(out_mode, "b"); break;
		case 't': fn_list = strdup(optarg); is_bamin = 0; break;
		case 'h': is_header = 1; break;
		case 'H': is_header_only = 1; break;
		case 'o': fn_out = strdup(optarg); break;
		case 'f': g_flag_on = strtol(optarg, 0, 0); break;
		case 'F': g_flag_off = strtol(optarg, 0, 0); break;
		case 'q': g_min_mapQ = atoi(optarg); break;
		default: return usage();
		}
	}
	if (is_header_only) is_header = 1;
	if (is_bamin) strcat(in_mode, "b");
	if (is_header) strcat(out_mode, "h");
	if (argc == optind) return usage();

	// open file handlers
	if ((in = samopen(argv[optind], in_mode, fn_list)) == 0) {
		fprintf(stderr, "[main_samview] fail to open file for reading.\n");
		goto view_end;
	}
	if ((out = samopen(fn_out? fn_out : "-", out_mode, in->header)) == 0) {
		fprintf(stderr, "[main_samview] fail to open file for writing.\n");
		goto view_end;
	}
	if (is_header_only) goto view_end; // no need to print alignments

	if (argc == optind + 1) { // convert/print the entire file
		bam1_t *b = bam_init1();
		int r;
		while ((r = samread(in, b)) >= 0) // read one alignment from `in'
			if (!__g_skip_aln(b))
				samwrite(out, b); // write the alignment to `out'
		if (r < -1) fprintf(stderr, "[main_samview] truncated file.\n");
		bam_destroy1(b);
	} else { // retrieve alignments in specified regions
		int i;
		bam_index_t *idx = 0;
		if (is_bamin) idx = bam_index_load(argv[optind]); // load BAM index
		if (idx == 0) { // index is unavailable
			fprintf(stderr, "[main_samview] random alignment retrieval only works for indexed BAM files.\n");
			ret = 1;
			goto view_end;
		}
		for (i = optind + 1; i < argc; ++i) {
			int tid, beg, end;
			bam_parse_region(in->header, argv[i], &tid, &beg, &end); // parse a region in the format like `chr2:100-200'
			if (tid < 0) { // reference name is not found
				fprintf(stderr, "[main_samview] fail to get the reference name. Continue anyway.\n");
				continue;
			}
			bam_fetch(in->x.bam, idx, tid, beg, end, out, view_func); // fetch alignments
		}
		bam_index_destroy(idx); // destroy the BAM index
	}

view_end:
	// close files, free and return
	free(fn_list); free(fn_out);
	samclose(in);
	samclose(out);
	return ret;
}

static int usage()
{
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage:   samtools view [options] <in.bam>|<in.sam> [region1 [...]]\n\n");
	fprintf(stderr, "Options: -b       output BAM\n");
	fprintf(stderr, "         -h       print header for the SAM output\n");
	fprintf(stderr, "         -H       print header only (no alignments)\n");
	fprintf(stderr, "         -S       input is SAM\n");
	fprintf(stderr, "         -t FILE  list of reference names and lengths (force -S) [null]\n");
	fprintf(stderr, "         -o FILE  output file name [stdout]\n");
	fprintf(stderr, "         -f INT   required flag, 0 for unset [0]\n");
	fprintf(stderr, "         -F INT   filtering flag, 0 for unset [0]\n");
	fprintf(stderr, "         -q INT   minimum mapping quality [0]\n");
	fprintf(stderr, "\n\
Notes:\n\
\n\
  1. By default, this command assumes the file on the command line is in\n\
     the BAM format and it prints the alignments in SAM. If `-t' is\n\
     applied, the input file is assumed to be in the SAM format. The\n\
     file supplied with `-t' is SPACE/TAB delimited with the first two\n\
     fields of each line consisting of the reference name and the\n\
     corresponding sequence length. The `.fai' file generated by `faidx'\n\
     can be used here. This file may be empty if reads are unaligned.\n\
\n\
  2. SAM->BAM conversion: `samtools view -bt ref.fa.fai in.sam.gz'.\n\
\n\
  3. BAM->SAM conversion: `samtools view in.bam'.\n\
\n\
  4. A region should be presented in one of the following formats:\n\
     `chr1', `chr2:1,000' and `chr3:1000-2,000'. When a region is\n\
     specified, the input alignment file must be an indexed BAM file.\n\
\n");
	return 1;
}

int main_import(int argc, char *argv[])
{
	int argc2, ret;
	char **argv2;
	if (argc != 4) {
		fprintf(stderr, "Usage: bamtk import <in.ref_list> <in.sam> <out.bam>\n");
		return 1;
	}
	argc2 = 6;
	argv2 = calloc(6, sizeof(char*));
	argv2[0] = "import", argv2[1] = "-o", argv2[2] = argv[3], argv2[3] = "-bt", argv2[4] = argv[1], argv2[5] = argv[2];
	ret = main_samview(argc2, argv2);
	free(argv2);
	return ret;
}
