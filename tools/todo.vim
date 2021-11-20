" This file is dedicated to the public domain.

function! Todo(...)
	if exists("a:1") && a:1 != ""
		copen
		" cex "" | cadde to avoid insta-jumping
		if &shell == "cmd.exe"
			cex "" | cadde system("git grep -nF TODO(".shellescape(a:1).")")
		else
			cex "" | cadde system("git grep -nF \"TODO(\"".shellescape(a:1)."\\)")
		endif
	else
		" just displaying like this for now...
		if &shell == "cmd.exe"
			" FIXME: write a Windows batch equivalent!? in the meantime, you
			" need a Unix shell to track issues :^)
			cex "" | cclose | !sh tools/todo
		else
			cex "" | cclose | !tools/todo
		endif
	endif
endfunction

function! TodoEdit(...)
	if exists("a:1") && a:1 != ""
		exec "tabe TODO/".a:1
		if line('$') == 1 && getline(1) == ''
			normal o====
			1
		else
			3 " XXX should really search for ====, but this is fine for now
		endif
	else
		echoerr "Specify an issue ID"
	endif
endfunction

command! -nargs=? Todo call Todo("<args>")
command! -nargs=? TodoEdit call TodoEdit("<args>")

" vi: sw=4 ts=4 noet tw=80 cc=80
