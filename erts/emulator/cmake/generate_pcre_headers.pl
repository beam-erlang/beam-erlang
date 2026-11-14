#!/usr/bin/env perl
# Generate the three PCRE yield/loop headers from pcre2_match.c.
# Line-for-line equivalent of erts/emulator/pcre/pcre.mk recipes.
use strict;
use warnings;

die "usage: generate_pcre_headers.pl SRC LOOP_H MEMCHR_H YIELD_H\n" unless @ARGV == 4;
my ($src, $loop_h, $memchr_h, $yield_h) = @ARGV;

open my $in, '<', $src or die "read $src: $!";
my @cost;
my @cost_with_dbg;
my @mem;
my $n = 0;
while (<$in>) {
    $n++;
    if (/COST_CHK\(/ && !/define/) {
        push @cost_with_dbg, $n;
        push @cost, $n unless /DBG_FAKE_/;
    }
    if (/MEMCHR_ERLANG\(/ && !/define/) {
        push @mem, $n;
    }
}
close $in;

open my $loop, '>', $loop_h or die "write $loop_h: $!";
for my $line (@cost) {
    print $loop "case $line: goto L_LOOP_COUNT_$line;\n";
}
close $loop;

open my $mem, '>', $memchr_h or die "write $memchr_h: $!";
for my $line (@mem) {
    print $mem "case $line: goto ERLANG_PCRE2_MATCH_YIELD_LINE_$line;\n";
}
close $mem;

open my $yield, '>', $yield_h or die "write $yield_h: $!";
my $index = 0;
# Yield coverage includes DBG_FAKE_COST_CHK lines (pcre.mk YIELD_COV);
# loop break cases omit them (pcre.mk GENINC).
for my $line (@cost_with_dbg, @mem) {
    print $yield "#define ERLANG_YIELD_POINT_$line $index\n";
    print $yield "$line,\n";
    $index++;
}
print $yield "#define ERLANG_YIELD_POINT_CNT $index\n";
close $yield;
