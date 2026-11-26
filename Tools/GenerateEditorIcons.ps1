# Generates the editor's default icons (128x128 PNG) into Editor/Resources/Icons.
# Users can replace any icon from Tools > Editor Settings > Icons without touching these files.
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\Editor\Resources\Icons')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Size = 128
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path

function Color([string]$hex, [int]$alpha = 255) {
    $c = [System.Drawing.ColorTranslator]::FromHtml($hex)
    return [System.Drawing.Color]::FromArgb($alpha, $c.R, $c.G, $c.B)
}

function RoundRectPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function Points([float[]]$values) {
    $points = New-Object 'System.Drawing.PointF[]' ($values.Length / 2)
    for ($i = 0; $i -lt $points.Length; $i++) {
        $points[$i] = New-Object System.Drawing.PointF($values[$i * 2], $values[$i * 2 + 1])
    }
    return ,$points
}

function WhitePen([float]$width = 9) {
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), $width
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    return $pen
}

function Save-Icon([string]$name, [scriptblock]$draw) {
    $bitmap = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    & $draw $g
    $g.Dispose()
    $bitmap.Save((Join-Path $OutputDirectory "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Host "  $name.png"
}

# Coloured rounded tile with a white glyph: component icons.
function Save-ComponentIcon([string]$name, [string]$color, [scriptblock]$glyph) {
    Save-Icon $name {
        param($g)
        $tile = RoundRectPath 6 6 116 116 24
        $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Point(0, 6)), (New-Object System.Drawing.Point(0, 122)), (Color $color), ([System.Drawing.Color]::FromArgb(255, [int]((Color $color).R * 0.78), [int]((Color $color).G * 0.78), [int]((Color $color).B * 0.78)))
        $g.FillPath($brush, $tile)
        & $glyph $g
    }
}

function DrawCube($g, $pen, [float]$cx, [float]$cy, [float]$s) {
    $top = Points @($cx, ($cy - $s), ($cx + $s * 0.87), ($cy - $s * 0.5), $cx, $cy, ($cx - $s * 0.87), ($cy - $s * 0.5))
    $g.DrawPolygon($pen, $top)
    $g.DrawLine($pen, ($cx - $s * 0.87), ($cy - $s * 0.5), ($cx - $s * 0.87), ($cy + $s * 0.5))
    $g.DrawLine($pen, ($cx + $s * 0.87), ($cy - $s * 0.5), ($cx + $s * 0.87), ($cy + $s * 0.5))
    $g.DrawLine($pen, $cx, $cy, $cx, ($cy + $s))
    $g.DrawLine($pen, ($cx - $s * 0.87), ($cy + $s * 0.5), $cx, ($cy + $s))
    $g.DrawLine($pen, ($cx + $s * 0.87), ($cy + $s * 0.5), $cx, ($cy + $s))
}

function DrawWave($g, $pen, [float]$y, [float]$amplitude) {
    $values = @()
    for ($x = 26; $x -le 102; $x += 2) {
        $values += $x
        $values += $y + [Math]::Sin(($x - 26) / 76.0 * 2 * [Math]::PI) * $amplitude
    }
    $g.DrawCurve($pen, (Points $values))
}

# Document with a coloured label band: file icons.
function Save-FileIcon([string]$name, [string]$color, [string]$label) {
    Save-Icon $name {
        param($g)
        $page = New-Object System.Drawing.Drawing2D.GraphicsPath
        $page.AddPolygon((Points @(22, 6, 80, 6, 106, 32, 106, 122, 22, 122)))
        $g.FillPath((New-Object System.Drawing.SolidBrush (Color '#E9EDF2')), $page)
        $g.DrawPath((New-Object System.Drawing.Pen (Color '#9AA4AF'), 3), $page)
        $g.FillPolygon((New-Object System.Drawing.SolidBrush (Color '#C3CAD3')), (Points @(80, 6, 80, 32, 106, 32)))
        $band = RoundRectPath 10 66 108 40 8
        $g.FillPath((New-Object System.Drawing.SolidBrush (Color $color)), $band)

        $fontSize = 26
        do {
            $font = New-Object System.Drawing.Font 'Arial', $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
            $measured = $g.MeasureString($label, $font)
            $fontSize -= 1
        } while ($measured.Width -gt 100 -and $fontSize -gt 10)
        $format = New-Object System.Drawing.StringFormat
        $format.Alignment = [System.Drawing.StringAlignment]::Center
        $format.LineAlignment = [System.Drawing.StringAlignment]::Center
        $g.DrawString($label, $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF(10, 67, 108, 40)), $format)
    }
}

Write-Host "Writing icons to $OutputDirectory"

# ---- Hierarchy ---------------------------------------------------------------------------------
Save-Icon 'gameobject' {
    param($g)
    $fill = Points @(64, 14, 107, 39, 107, 89, 64, 114, 21, 89, 21, 39)
    $g.FillPolygon((New-Object System.Drawing.SolidBrush (Color '#6FA8E8' 70)), $fill)
    $pen = New-Object System.Drawing.Pen (Color '#8CC0F5'), 9
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    DrawCube $g $pen 64 64 50
}

# ---- Components --------------------------------------------------------------------------------
Save-ComponentIcon 'component_transform' '#4F86D6' {
    param($g)
    $pen = WhitePen 9
    $g.DrawLine($pen, 64, 26, 64, 102); $g.DrawLine($pen, 26, 64, 102, 64)
    foreach ($tip in @(@(64, 24, 52, 38, 76, 38), @(64, 104, 52, 90, 76, 90), @(24, 64, 38, 52, 38, 76), @(104, 64, 90, 52, 90, 76))) {
        $g.FillPolygon([System.Drawing.Brushes]::White, (Points $tip))
    }
}
Save-ComponentIcon 'component_mesh' '#33A39B' { param($g); DrawCube $g (WhitePen 8) 64 64 40 }
Save-ComponentIcon 'component_skinned_mesh' '#7E63C9' {
    param($g)
    $pen = WhitePen 9
    $g.DrawEllipse($pen, 52, 20, 24, 24)
    $g.DrawLine($pen, 64, 46, 64, 78)
    $g.DrawLine($pen, 40, 56, 88, 56)
    $g.DrawLine($pen, 64, 78, 46, 106); $g.DrawLine($pen, 64, 78, 82, 106)
}
Save-ComponentIcon 'component_camera' '#61707F' {
    param($g)
    $pen = WhitePen 8
    $g.DrawPath($pen, (RoundRectPath 20 42 66 48 8))
    $g.DrawPolygon($pen, (Points @(86, 56, 108, 44, 108, 88, 86, 76)))
    $g.DrawEllipse($pen, 34, 22, 20, 20); $g.DrawEllipse($pen, 56, 22, 20, 20)
}
Save-ComponentIcon 'component_light' '#D9A82E' {
    param($g)
    $pen = WhitePen 8
    $g.DrawEllipse($pen, 44, 44, 40, 40)
    for ($i = 0; $i -lt 8; $i++) {
        $a = $i * [Math]::PI / 4
        $g.DrawLine($pen, (64 + [Math]::Cos($a) * 30), (64 + [Math]::Sin($a) * 30), (64 + [Math]::Cos($a) * 42), (64 + [Math]::Sin($a) * 42))
    }
}
Save-ComponentIcon 'component_audio' '#D6742F' {
    param($g)
    $pen = WhitePen 8
    $g.FillPolygon([System.Drawing.Brushes]::White, (Points @(24, 50, 42, 50, 64, 30, 64, 98, 42, 78, 24, 78)))
    $g.DrawArc($pen, 60, 44, 24, 40, -50, 100)
    $g.DrawArc($pen, 58, 30, 44, 68, -50, 100)
}
Save-ComponentIcon 'component_script' '#3F9450' {
    param($g)
    $pen = WhitePen 9
    $g.DrawLines($pen, (Points @(44, 40, 24, 64, 44, 88)))
    $g.DrawLines($pen, (Points @(84, 40, 104, 64, 84, 88)))
    $g.DrawLine($pen, 72, 34, 56, 94)
}
Save-ComponentIcon 'component_animator' '#C24F86' {
    param($g)
    $g.DrawEllipse((WhitePen 8), 24, 24, 80, 80)
    $g.FillPolygon([System.Drawing.Brushes]::White, (Points @(54, 44, 86, 64, 54, 84)))
}
Save-ComponentIcon 'component_rigidbody' '#CC4747' {
    param($g)
    $g.FillEllipse([System.Drawing.Brushes]::White, 54, 40, 48, 48)
    $pen = WhitePen 8
    $g.DrawLine($pen, 22, 50, 42, 50); $g.DrawLine($pen, 16, 64, 42, 64); $g.DrawLine($pen, 22, 78, 42, 78)
}
Save-ComponentIcon 'component_collider' '#6DB33F' {
    param($g)
    $pen = WhitePen 7
    $pen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
    $g.DrawRectangle($pen, 26, 26, 76, 76)
    $g.DrawEllipse((WhitePen 8), 44, 44, 40, 40)
}
Save-ComponentIcon 'component_joint' '#A9743A' {
    param($g)
    $pen = WhitePen 8
    $g.DrawEllipse($pen, 22, 66, 34, 34); $g.DrawEllipse($pen, 72, 28, 34, 34)
    $g.DrawLine($pen, 50, 72, 78, 56)
}
Save-ComponentIcon 'component_particle' '#D15FBE' {
    param($g)
    foreach ($dot in @(@(30, 34, 14), @(70, 24, 10), @(88, 58, 16), @(42, 76, 18), @(80, 96, 10), @(26, 100, 8))) {
        $g.FillEllipse([System.Drawing.Brushes]::White, $dot[0], $dot[1], $dot[2], $dot[2])
    }
}
Save-ComponentIcon 'component_terrain' '#5E9445' {
    param($g)
    $g.FillPolygon([System.Drawing.Brushes]::White, (Points @(16, 100, 50, 46, 66, 70, 82, 52, 112, 100)))
    $g.FillEllipse([System.Drawing.Brushes]::White, 84, 22, 18, 18)
}
Save-ComponentIcon 'component_water' '#2F84D3' {
    param($g)
    $pen = WhitePen 8
    DrawWave $g $pen 46 10; DrawWave $g $pen 70 10; DrawWave $g $pen 94 10
}
Save-ComponentIcon 'component_wave' '#22A9BE' {
    param($g)
    $pen = WhitePen 7
    $g.DrawEllipse($pen, 54, 54, 20, 20); $g.DrawEllipse($pen, 38, 38, 52, 52); $g.DrawEllipse($pen, 22, 22, 84, 84)
}
Save-ComponentIcon 'component_fog' '#7F92A6' {
    param($g)
    $pen = WhitePen 9
    $g.DrawLine($pen, 28, 40, 92, 40); $g.DrawLine($pen, 40, 64, 104, 64); $g.DrawLine($pen, 24, 88, 84, 88)
}
Save-ComponentIcon 'component_skybox' '#3C97D6' {
    param($g)
    $g.FillEllipse((New-Object System.Drawing.SolidBrush (Color '#FFE27A')), 24, 24, 38, 38)
    $cloud = New-Object System.Drawing.Drawing2D.GraphicsPath
    $cloud.FillMode = [System.Drawing.Drawing2D.FillMode]::Winding
    $cloud.AddEllipse(40, 60, 34, 34); $cloud.AddEllipse(60, 48, 40, 40); $cloud.AddEllipse(82, 66, 26, 26)
    $cloud.AddRectangle((New-Object System.Drawing.RectangleF(56, 74, 40, 20)))
    $g.FillPath([System.Drawing.Brushes]::White, $cloud)
}
Save-ComponentIcon 'component_wind' '#5FB3B6' {
    param($g)
    $pen = WhitePen 8
    $g.DrawLine($pen, 20, 50, 78, 50); $g.DrawArc($pen, 70, 30, 24, 20, 90, -270)
    $g.DrawLine($pen, 20, 76, 92, 76); $g.DrawArc($pen, 82, 76, 22, 22, -90, 270)
}
Save-ComponentIcon 'component_state_machine' '#8B5FCF' {
    param($g)
    $pen = WhitePen 7
    $g.DrawLine($pen, 40, 40, 88, 40); $g.DrawLine($pen, 40, 40, 64, 90); $g.DrawLine($pen, 88, 40, 64, 90)
    foreach ($node in @(@(26, 26), @(74, 26), @(50, 76))) { $g.FillEllipse([System.Drawing.Brushes]::White, $node[0], $node[1], 28, 28) }
}
Save-ComponentIcon 'component_default' '#6E7580' {
    param($g)
    $pen = WhitePen 8
    for ($i = 0; $i -lt 8; $i++) {
        $a = $i * [Math]::PI / 4
        $g.DrawLine($pen, (64 + [Math]::Cos($a) * 30), (64 + [Math]::Sin($a) * 30), (64 + [Math]::Cos($a) * 40), (64 + [Math]::Sin($a) * 40))
    }
    $g.DrawEllipse($pen, 36, 36, 56, 56)
    $g.DrawEllipse($pen, 54, 54, 20, 20)
}

# ---- Files -------------------------------------------------------------------------------------
Save-Icon 'file_folder' {
    param($g)
    $back = New-Object System.Drawing.Drawing2D.GraphicsPath
    $back.AddPolygon((Points @(8, 28, 48, 28, 58, 40, 120, 40, 120, 110, 8, 110)))
    $g.FillPath((New-Object System.Drawing.SolidBrush (Color '#C99A2E')), $back)
    $g.FillPath((New-Object System.Drawing.SolidBrush (Color '#F0C04F')), (RoundRectPath 8 50 112 62 8))
}
Save-FileIcon 'file_scene' '#3D7BD9' 'SCENE'
Save-FileIcon 'file_prefab' '#2E9BDB' 'PREFAB'
Save-FileIcon 'file_material' '#E07A3A' 'MAT'
Save-FileIcon 'file_mesh' '#33A39B' 'MESH'
Save-FileIcon 'file_model' '#2F8F6F' 'MODEL'
Save-FileIcon 'file_texture' '#9B57CF' 'IMG'
Save-FileIcon 'file_audio' '#D9A13B' 'AUDIO'
Save-FileIcon 'file_effect' '#D955AE' 'FX'
Save-FileIcon 'file_animation' '#C24F86' 'ANIM'
Save-FileIcon 'file_shader' '#5D63D1' 'HLSL'
Save-FileIcon 'file_code' '#3F9450' 'C++'
Save-FileIcon 'file_json' '#7D8794' 'JSON'
Save-FileIcon 'file_text' '#95A0AB' 'TXT'
Save-FileIcon 'file_font' '#A9743A' 'FONT'
Save-FileIcon 'file_archive' '#8C6D4F' 'ZIP'
Save-FileIcon 'file_binary' '#61707F' 'BIN'
Save-FileIcon 'file_default' '#6E7580' 'FILE'

Write-Host 'Done.'
