<# : This script file is dedicated to the public domain. SST itself is not.
:: Note: the control flow in this file is pretty wacky, and for that, I apologise.
@echo off
pushd "%~dp0"
powershell -WindowStyle hidden -NoProfile -NoLogo -ExecutionPolicy bypass "iex (${%~f0} | out-string)"
if exist .sst-update.zip (
	del .sst-update.zip
	if exist sst-postupdate.bat (
		call sst-postupdate.bat
		del sst-postupdate.bat
	)
	:: if updated from game launch script, plugin can find out and display a message
	set SST_UPDATED=1
)
popd
goto :eof
#>
$ProgressPreference = "SilentlyContinue"
[System.Reflection.Assembly]::LoadWithPartialName('System.Windows.Forms') >$null
$url = [IO.File]::ReadAllText("update-url").Trim()
$realurl = $url
$origurl = $url
$h = @{}
try {
	$etag = [IO.File]::ReadAllText("update-version")
	$h["If-None-Match"] = $etag
}
catch {}
function errorbox {
	[System.Windows.Forms.MessageBox]::Show('Failed to check for SST updates. You may or may not be up to date!', 'Update error') >$null
}
for ($i = 0; $i -lt 10; $i++) {
	$r = Invoke-WebRequest -UseBasicParsing -UserAgent "sstupdate v2" -Uri $url -OutFile ".sst-update.zip" -PassThru -Headers $h -MaximumRedirection 0 -ErrorAction SilentlyContinue
	if ($r.StatusCode -eq 301) {
		$url = $r.Headers.Location
		$realurl = $url
		continue
	}
	if ($r.StatusCode -eq 302) { # don't immediately save a Found.
		$url = $r.Headers.Location
		continue
	}
	if ($r.StatusCode -eq 200 -or $r.StatusCode -eq 304) {
		if ($r.Headers.ETag) { [IO.File]::WriteAllText("update-version", $r.Headers.ETag) }
		if ($realurl -ne $origurl) { [IO.File]::WriteAllText("update-url", $realurl) }
		if ($r.StatusCode -eq 200) { Expand-Archive -Force -DestinationPath . .sst-update.zip }
		else { del .sst-update.zip } # stupid but oh well
		exit
	}
	errorbox
	exit
}
errorbox # too many redirects
