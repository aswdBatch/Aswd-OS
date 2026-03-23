param(
    [Parameter(Mandatory = $true)]
    [string]$KernelBin,

    [Parameter(Mandatory = $true)]
    [string]$Output
)

$python = "C:\Users\gusta\AppData\Local\Programs\Python\Python311\python.exe"
$kernelPath = if ([System.IO.Path]::IsPathRooted($KernelBin)) { $KernelBin } else { Join-Path $PSScriptRoot "..\$KernelBin" }
$outputPath = if ([System.IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path $PSScriptRoot "..\$Output" }
& $python (Join-Path $PSScriptRoot "make_usb_image.py") --kernel-bin $kernelPath --output $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "USB image build failed."
}
