if [ $# -ge 1 ]; then
	search="$1"
else
	search="."
fi
find $search \( -name '*.c' -or -name '*.h' -or -name 'module.*' \) \
	-exec sh -c 'if [ -x {} ]; then echo {}; fi' \; \
	| while read filename; do
	echo "$filename"
#	svn propset svn:execute off "$filename"
done
