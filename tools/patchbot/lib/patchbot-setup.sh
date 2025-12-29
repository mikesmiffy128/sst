# This file is dedicated to the public domain.

this_project=sst

case "`uname`" in
	*_NT*)
		# make sure we use windows curl because Git's bundled curl might be
		# really old
		curl="$SYSTEMROOT/System32/curl.exe"
		tokendir="$LOCALAPPDATA/PatchBot"
		devtty="&0" ;; # /dev/tty doesn't seem to work even with Git's MSYS Bash
	*)
		curl=curl
		# this isn't technically guaranteed to be installed
		if ! command -v curl >/dev/null 2>&1; then
			printf "patchbot-setup: fatal: curl is not installed\n" >&2
			exit 2
		fi
		tokendir="${XDG_STATE_HOME:-~/.local/state}/patchbot"
		devtty=/dev/tty ;;
esac

urltofilename() {
	_str="$1"
	_ret=""
	while [ -n "$_str" ]; do
		_next="${_str#?}"
		case "$_str" in
			[a-z0-0\.,\-+=@\$]*) _ret="$_ret${_str%"$_next"}" ;;
			_*) _ret="${_ret}__" ;;
			*) _ret="$_ret`printf '_%02x' "'$_str"`" ;;
		esac
		_str="$_next"
	done
	echo "$_ret"
}

gitdir="`git rev-parse --git-dir`"

token=""
server="`git config --default "https://patchbot.mikes.software" PatchBot.server`"
: "${projectid:="`git config --default $this_project PatchBot.projectid`"}"
remote_branch="`git config --default origin/master PatchBot.remoteBranch`"
tokenfile="$tokendir/token_`urltofilename "$server"`"
if [ -f "$tokenfile" ]; then token="`cat "$tokenfile"`"; fi

setupauth() {
	printf "NOTE: PatchBot auth setup currently has to be done manually.\n" >&2
	printf "Get yourself a token via the Tecknoledgy Committy, and paste it here:\n" >&2
	printf "> " >&2
	eval "read -r token <$devtty" # have to eval for &0 to work
	if [ -z "$token" ]; then
		printf "No token provided, exiting\n" >&2;
		exit 1;
	fi
	mkdir -p "$tokendir"
	printf '%s\n' "$token" > "$tokenfile"
	printf 'Note: saved token to %s - please DO NOT share this!\n' "$tokenfile" >&2
}

# vi: sw=4 ts=4 noet tw=80 cc=80
