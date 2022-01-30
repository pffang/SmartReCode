param(
    [string]$BuildDirectory = "build\msix-x64",
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$CertificateThumbprint = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $root $BuildDirectory))
}
$cache = Join-Path $build "CMakeCache.txt"
if (!(Test-Path -LiteralPath $cache)) {
    throw "CMake build directory not configured: $build"
}

$cacheText = Get-Content -LiteralPath $cache
$generatorLine = $cacheText | Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' } | Select-Object -First 1
if (!$generatorLine) { throw "CMake generator not found in $cache" }
$generator = ($generatorLine -split '=', 2)[1]
$multiConfig = $generator -match '^(Visual Studio|Ninja Multi-Config|Xcode)'
$singleConfig = $cacheText | Where-Object { $_ -match '^CMAKE_BUILD_TYPE:STRING=' } | Select-Object -First 1
if (!$multiConfig -and (!$singleConfig -or ($singleConfig -split '=', 2)[1] -ne $Configuration)) {
    throw "This single-config build directory is not configured for $Configuration."
}
& cmake --build $build --config $Configuration --target SmartRecoder
if ($LASTEXITCODE -ne 0) { throw "CMake build failed: $LASTEXITCODE" }

$output = if ($multiConfig) { Join-Path $build $Configuration } else { $build }
$exe = Join-Path $output "SmartRecoder.exe"
if (!(Test-Path -LiteralPath $exe)) { throw "Executable not found: $exe" }

$stream = [IO.File]::OpenRead($exe)
try {
    $reader = [IO.BinaryReader]::new($stream)
    if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a valid PE executable: $exe" }
    $stream.Position = 0x3c
    $offset = $reader.ReadUInt32()
    $stream.Position = $offset
    if ($reader.ReadUInt32() -ne 0x00004550) { throw "Not a valid PE executable: $exe" }
    $machine = $reader.ReadUInt16()
} finally {
    $stream.Dispose()
}
$architecture = switch ($machine) {
    0x8664 { "x64" }
    0xaa64 { "arm64" }
    default { throw ("Unsupported executable architecture: 0x{0:X4}" -f $machine) }
}

$kitsRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$sdkBins = Join-Path $kitsRoot "bin"
$makeappx = Get-ChildItem -LiteralPath $sdkBins -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\makeappx.exe' } |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$makeappx) { throw "Windows SDK makeappx.exe was not found." }
$signtool = Join-Path (Split-Path $makeappx) "signtool.exe"
if ($CertificateThumbprint -and !(Test-Path -LiteralPath $signtool)) {
    throw "Windows SDK signtool.exe was not found."
}
$runtime = Join-Path $root ".winapp\bin\$architecture"
if (!(Test-Path -LiteralPath (Join-Path $runtime "Microsoft.WindowsAppRuntime.Bootstrap.dll"))) {
    throw "Windows App SDK runtime files missing for $architecture. Run winapp restore."
}

$stage = Join-Path $build ("msix-stage-" + [guid]::NewGuid().ToString("N"))
$package = Join-Path $build "SmartRecoder-$architecture.msix"
$temporaryPackage = Join-Path $build ("msix-" + [guid]::NewGuid().ToString("N") + ".msix")
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    Copy-Item -LiteralPath $exe -Destination $stage
    Copy-Item -Path (Join-Path $runtime "*") -Destination $stage -Recurse
    $manifest = [IO.File]::ReadAllText((Join-Path $root "Package.appxmanifest"))
    $manifest = $manifest.Replace("@ARCH@", $architecture)
    [IO.File]::WriteAllText((Join-Path $stage "AppxManifest.xml"), $manifest, [Text.UTF8Encoding]::new($false))

    Add-Type -AssemblyName System.Drawing
    $assets = Join-Path $stage "Assets"
    New-Item -ItemType Directory -Path $assets | Out-Null
    foreach ($image in @(
        @{ Name = "StoreLogo.png"; Size = 50 },
        @{ Name = "Square44x44Logo.png"; Size = 44 },
        @{ Name = "Square150x150Logo.png"; Size = 150 }
    )) {
        $bitmap = [Drawing.Bitmap]::new($image.Size, $image.Size)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $font = [Drawing.Font]::new("Segoe UI", [single]($image.Size * 0.30), [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
        $format = [Drawing.StringFormat]::new()
        try {
            $graphics.Clear([Drawing.Color]::FromArgb(23, 97, 160))
            $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
            $format.Alignment = [Drawing.StringAlignment]::Center
            $format.LineAlignment = [Drawing.StringAlignment]::Center
            $rect = [Drawing.RectangleF]::new(0, 0, $image.Size, $image.Size)
            $graphics.DrawString("SR", $font, [Drawing.Brushes]::White, $rect, $format)
            $bitmap.Save((Join-Path $assets $image.Name), [Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $format.Dispose()
            $font.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }

    & $makeappx pack /d $stage /p $temporaryPackage
    if ($LASTEXITCODE -ne 0) { throw "MSIX packing failed: $LASTEXITCODE" }
    if ($CertificateThumbprint) {
        & $signtool sign /fd SHA256 /sha1 $CertificateThumbprint $temporaryPackage
        if ($LASTEXITCODE -ne 0) { throw "MSIX signing failed: $LASTEXITCODE" }
        & $signtool verify /pa $temporaryPackage
        if ($LASTEXITCODE -ne 0) { throw "MSIX signature verification failed: $LASTEXITCODE. Check the certificate trust chain." }
    } else {
        Write-Warning "Package is unsigned and cannot be installed until signed."
    }
    Move-Item -LiteralPath $temporaryPackage -Destination $package -Force
    Write-Output "MSIX: $package"
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force
    if (Test-Path -LiteralPath $temporaryPackage) { Remove-Item -LiteralPath $temporaryPackage -Force }
}
