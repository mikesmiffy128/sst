# This file is dedicated to the public domain.

todo() {
	if [ $# = 0 ]; then
		printf "Active TODO list:\n\n"
		ls TODO/ | {
			while read _l; do
				printf "  * %s: " "$_l"
				head -n1 "TODO/$_l"
			done
		}
		return
	fi
	if [ $# != 1 ]; then
		echo "expected 0 or 1 argument(s)"
		return
	fi
	if [ -f "TODO/$1" ]; then
		printf "Active TODO item: "
		_f="TODO/$1"
	elif [ -f "TODO/.$1" ]; then
		printf "Inactive TODO item: "
		_f="TODO/.$1"
	else
		echo "TODO item not found: $1"
		return
	fi
	head -n1 "$_f"
	printf "\n"
	sed -n '/^====$/,$p' "$_f"
	printf "====\n\nMentions in project:\n"
	git grep -Fn "TODO($1)" || echo "<none>"
}

# vi: sw=4 ts=4 noet tw=80 cc=80
