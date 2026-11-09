# Regenerates resources/app.ico (a simple sun) with ImageMagick 7 ("magick" must be on PATH).

$ErrorActionPreference = 'Stop'

$output = Join-Path $PSScriptRoot '..\resources\app.ico'
$color = '#FFB300'

# Eight rays around the center (128, 128) of a 256 x 256 canvas.
$rays = foreach ($angle in 0, 45, 90, 135, 180, 225, 270, 315) {
    $radians = $angle * [Math]::PI / 180
    $x1 = 128 + 82 * [Math]::Cos($radians)
    $y1 = 128 + 82 * [Math]::Sin($radians)
    $x2 = 128 + 116 * [Math]::Cos($radians)
    $y2 = 128 + 116 * [Math]::Sin($radians)
    '-draw', ('line {0:F1},{1:F1} {2:F1},{3:F1}' -f $x1, $y1, $x2, $y2)
}

magick -size 256x256 xc:none `
    -fill $color -stroke none -draw 'circle 128,128 128,72' `
    -stroke $color -strokewidth 22 -fill none -draw 'stroke-linecap round' @rays `
    -define icon:auto-resize=256,64,48,32,24,20,16 `
    $output

Write-Host "Written $output"
