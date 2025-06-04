# This script is dedicated to the public domain.

Add-Type -Assembly System.IO.Compression.FileSystem

$OutDir = "tools\windbg\bin"
$Arch = "x64" # can also use x86, arm64

if (!(Test-Path $OutDir)) { $null = mkdir $OutDir }
[xml]$content = (New-Object System.Net.WebClient).DownloadString("https://aka.ms/windbg/download")
$bundleurl = $content.AppInstaller.MainBundle.Uri
# Using curl.exe here instead because it has an actual useful progress bar.
# Modern PowerShell does too, but not the PS 5.1 that still ships with W10
#Invoke-WebRequest $bundleurl -OutFile $OutDir\__bundle.zip
curl.exe "-#o$OutDir\__bundle.zip" "$bundleurl"
$filename = switch ($Arch) {
	"x64" { "windbg_win-x64.msix" }
	"x86" { "windbg_win-x86.msix" }
	"arm64" { "windbg_win-arm64.msix" }
}
$zip = [IO.Compression.ZipFile]::OpenRead("$OutDir\__bundle.zip")
try {
	if ($found = $zip.Entries.Where({ $_.FullName -eq $filename }, "First") ) {
	    $dest = "$OutDir\__msix.zip"
	    [IO.Compression.ZipFileExtensions]::ExtractToFile($found[0], $dest, $false)
	}
	else {
	    Write-Error "File not found in ZIP: $filename"
	    exit 100
	}
}
finally {
	if ($zip) { $zip.Dispose() }
}
rm $OutDir\__bundle.zip
Expand-Archive -DestinationPath "$OutDir" "$OutDir\__msix.zip"
rm $OutDir\__msix.zip
# misc cleanup
rm -r $OutDir\AppxMetadata\
rm $OutDir\'``[Content_Types``].xml' # wtf, microsoft, wtf.
rm $OutDir\AppxBlockMap.xml
rm $OutDir\AppxManifest.xml
rm $OutDir\AppxSignature.p7x
rm -r $OutDir\runtimes\unix\
mv "$OutDir\Third%20Party%20Notices.txt" "$OutDir\Third Party Notices.txt"
