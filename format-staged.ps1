# Run clang-format on all staged C/C++ files, re-stage them, and optionally commit.
# Usage:
#   .\format-staged.ps1                   # format and re-stage only
#   .\format-staged.ps1 "commit message"  # format, re-stage, and commit

param(
    [Parameter(Position=0)]
    [string]$CommitMessage
)

$extensions = @('.c', '.cpp', '.h', '.hpp')

$staged = git diff --cached --name-only | Where-Object {
    $ext = [System.IO.Path]::GetExtension($_)
    $extensions -contains $ext
}

if (-not $staged) {
    Write-Host "No staged C/C++ files to format."
    exit 0
}

foreach ($file in $staged) {
    Write-Host "Formatting $file ..."
    clang-format -i $file
}

git add $staged

Write-Host "Done. All staged C/C++ files formatted and re-staged."

if ($CommitMessage) {
    git commit -m $CommitMessage
} else {
    Write-Host "No commit message provided — skipping commit."
}
