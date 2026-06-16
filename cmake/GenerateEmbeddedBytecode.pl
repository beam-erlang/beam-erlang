#!/usr/bin/env perl
# SPDX-License-Identifier: Apache-2.0
use strict;
use warnings;

my ($output, $kind, @modules);
while (@ARGV) {
    my $arg = shift @ARGV;
    if ($arg eq '--component') { $kind = shift @ARGV; }
    elsif ($arg eq '--output') { $output = shift @ARGV; }
    elsif ($arg eq '--module') {
        my ($name, $path, $nif) = splice(@ARGV, 0, 3);
        die "invalid embedded module\n" unless defined($nif) && $name =~ /^[a-z][a-z0-9_]*$/;
        die "invalid NIF entry\n" unless $nif eq '-' || $nif =~ /^[A-Za-z_][A-Za-z0-9_]*$/;
        push @modules, [$name, $path, $nif];
    } else { die "unknown argument: $arg\n"; }
}
die "provide a mode, output and modules\n" unless $kind && $output && @modules;
die "invalid component kind\n" if $kind !~ /^BEAM_COMPONENT_(FILE|NETWORK|ZLIB)$/;
open(my $out, '>', $output) or die "cannot write $output: $!\n";
print $out "/* Generated embedded bytecode. */\n";
print $out "#include \"beam_component_internal.h\"\n#include \"beam_component_build.h\"\n";
for my $module (@modules) {
    my ($name, $path) = @$module;
    open(my $in, '<:raw', $path) or die "cannot read $path: $!\n";
    local $/;
    my $bytes = <$in>;
    close($in);
    die "not a BEAM file: $path\n" unless substr($bytes,0,4) eq 'FOR1' && substr($bytes,8,4) eq 'BEAM';
    print $out "static const unsigned char bytecode_$name\[\] = {\n";
    my @octets = unpack('C*', $bytes);
    for (my $i=0; $i<@octets; $i+=16) {
        my $end = $i+15 < $#octets ? $i+15 : $#octets;
        print $out '    ', join('', map {sprintf('0x%02x,', $_)} @octets[$i..$end]), "\n";
    }
    print $out "};\n";
}
print $out "static const beam_component_module modules[] = {\n";
for (@modules) {
    my ($n, $path, $nif)=@$_;
    my $entry=$nif eq '-' ? 'NULL' : qq{"$nif"};
    print $out "    {\"$n\", bytecode_$n, sizeof(bytecode_$n), $entry},\n";
}
print $out "};\nconst beam_component_descriptor beam_component_descriptor_v1 = {\n";
my $dependencies = '0';
print $out "    BEAM_COMPONENT_ABI_VERSION, BEAM_COMPONENT_BUILD_ID, $kind, sizeof(modules) / sizeof(modules[0]), modules,\n";
print $out "    BEAM_COMPONENT_BASE_VERSION, $dependencies\n};\n";
close($out) or die "cannot finish $output: $!\n";
