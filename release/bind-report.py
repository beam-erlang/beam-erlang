"""Bind producer test evidence after the immutable candidate record is written."""
import sys
import release
report = release.read(sys.argv[1])
release.validate_candidate_version(release.read(sys.argv[2]))
report['candidate'] = release.digest(sys.argv[2])
release.write(sys.argv[1], report)
