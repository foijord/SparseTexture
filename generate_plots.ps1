# Check if required assemblies are available
try {
    Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
    Add-Type -AssemblyName System.Windows.Forms.DataVisualization -ErrorAction Stop
}
catch {
    Write-Error "Failed to load required assemblies. Ensure Microsoft Chart Controls for .NET Framework are installed. Error: $_"
    exit 1
}

$folderPath = "Runs"

# Define the README.md path in the script's execution directory
$readmePath = "README.md"

# Prepend "Runs/" and handle spaces
$markdownPath = "$folderPath/".Replace(' ', '%20')

# Get all .txt files in the folder
$dataFiles = Get-ChildItem -Path $folderPath -Filter "*.txt" | Sort-Object Name

if ($dataFiles.Count -eq 0) {
    Write-Warning "No .txt files found in '$folderPath'."
    exit 0
}

Write-Host "Found $($dataFiles.Count) .txt files in '$folderPath'."

# Initialize list to track generated PNGs
$generatedPngs = @()
$successCount = 0

# Process each data file
foreach ($file in $dataFiles) {
    $filePath = $file.FullName
    Write-Host "Processing '$filePath'..."

    # Generate output PNG path in the same directory as the input file
    $outputImage = [System.IO.Path]::ChangeExtension($filePath, ".png")
    Write-Host "Output will be saved to: $outputImage"

    # Read and validate data
    try {
        # Read all lines from the file
        $lines = Get-Content -Path $filePath -ErrorAction Stop
        if ($lines.Count -lt 2) {
            Write-Warning "Skipping '$filePath': File must contain at least two lines (title and one or more numbers)."
            continue
        }

        # First line is the chart title
        $chartTitle = $lines[0].Trim()
        Write-Host "Raw first line read: '$($lines[0])'"
        Write-Host "Processed chart title: '$chartTitle'"
        if ([string]::IsNullOrWhiteSpace($chartTitle)) {
            Write-Warning "First line in '$filePath' is empty or whitespace. Using default title 'Chart'."
            $chartTitle = "Chart"
        }

        # Process remaining lines as numbers
        $data = $lines | Select-Object -Skip 1 | 
                Where-Object { $_ -match '^-?\d*\.?\d*$' } | 
                ForEach-Object { [double]$_ }
        if ($data.Count -eq 0) {
            Write-Warning "Skipping '$filePath': No valid numeric data found after the first line."
            continue
        }
        Write-Host "Read $($data.Count) valid numbers from the file."
    }
    catch {
        Write-Warning "Skipping '$filePath': Error reading or parsing file. Error: $_"
        continue
    }

    # Create a chart object
    try {
        $chart = New-Object System.Windows.Forms.DataVisualization.Charting.Chart -ErrorAction Stop
        $chart.Width = 800
        $chart.Height = 600
        $chart.BackColor = [System.Drawing.Color]::White

        # Add chart title with explicit configuration
        $title = New-Object System.Windows.Forms.DataVisualization.Charting.Title
        $title.Text = $chartTitle
        $title.Font = New-Object System.Drawing.Font("Arial", 16, [System.Drawing.FontStyle]::Bold)
        $title.ForeColor = [System.Drawing.Color]::Black
        $title.Alignment = [System.Drawing.ContentAlignment]::TopCenter
        $chart.Titles.Add($title)
        Write-Host "Title added to chart: '$chartTitle'"

        # Create a chart area
        $chartArea = New-Object System.Windows.Forms.DataVisualization.Charting.ChartArea
        $chartArea.Name = "ChartArea1"
        $chartArea.AxisX.Title = "Sample"
        $chartArea.AxisX.LabelStyle.Enabled = $false  # Disable x-axis labels (line numbers)
        $chartArea.AxisY.Title = "Value"
        $chart.ChartAreas.Add($chartArea)

        # Create a series for the line plot
        $series = New-Object System.Windows.Forms.DataVisualization.Charting.Series
        $series.Name = "Data"
        $series.ChartType = [System.Windows.Forms.DataVisualization.Charting.SeriesChartType]::Line
        $series.BorderWidth = 2
        $series.Color = [System.Drawing.Color]::Blue

        # Add data points
        for ($i = 0; $i -lt $data.Length; $i++) {
            $series.Points.AddXY($i + 1, $data[$i])
        }

        # Add the series to the chart
        $chart.Series.Add($series)

        # Save the chart as an image
        try {
            $chart.SaveImage($outputImage, [System.Windows.Forms.DataVisualization.Charting.ChartImageFormat]::Png)
            Write-Host "Plot successfully saved to '$outputImage'"
            $generatedPngs += [System.IO.Path]::GetFileName($outputImage)
            $successCount++
        }
        catch {
            Write-Warning "Skipping '$filePath': Failed to save plot to '$outputImage'. Check write permissions or path validity. Error: $_"
            continue
        }
    }
    catch {
        Write-Warning "Skipping '$filePath': Error creating chart. Error: $_"
        continue
    }
}

Write-Host "Completed processing. Successfully generated $successCount PNG(s)."

# Update README.md with PNG references
if ($generatedPngs.Count -gt 0) {
    Write-Host "Updating '$readmePath' with PNG references..."

    # Create Markdown image references with file name as alt text
    $imageMarkdown = $generatedPngs | Sort-Object | ForEach-Object { 
        $encodedFile = $_.Replace(' ', '%20')
        "![$_]($markdownPath/$encodedFile)" 
    }
    $newSection = $imageMarkdown -join "`n"

    try {
        # If README.md exists, check for existing PNG Images section
        if (Test-Path -Path $readmePath) {
            $existingContent = Get-Content -Path $readmePath -Raw
            # Remove existing PNG Images section if present (for backward compatibility)
            $sectionMarker = "## PNG Images"
            $updatedContent = $existingContent -replace "(?s)$sectionMarker.*?(?=\n##|\Z)", ""
            # Append new image references
            $updatedContent = $updatedContent.TrimEnd() + "`n`n" + $newSection
            Set-Content -Path $readmePath -Value $updatedContent -ErrorAction Stop
        }
        else {
            # Create new README.md with the image references
            Set-Content -Path $readmePath -Value $newSection -ErrorAction Stop
        }
        Write-Host "Successfully updated '$readmePath' with $($generatedPngs.Count) PNG references."
    }
    catch {
        Write-Error "Failed to update '$readmePath'. Check write permissions or path validity. Error: $_"
        exit 1
    }
}
else {
    Write-Warning "No PNGs generated, so '$readmePath' was not updated."
}