# begin of script
echo "Assignment Setup Script .ver 0.1"

# download and install required software
echo "Installing Required Software Packages..."

# package manager
Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# cmake
choco install cmake --installargs 'ADD_CMAKE_TO_PATH=System' -y

# command line git
choco install git.install --params "/GitAndUnixToolsOnPath /WindowsTerminal /NoAutoCrlf" -y 

# refresh PATH vars
refreshenv
echo "Software Installation Complete"

# create solution with cmake
echo "Building IDE Solution with CMake..."
Push-Location $PSScriptRoot
cmake -A x64 -S ./ -B ./Solution
Pop-Location

echo "Assignment Setup Script complete"
# Comment back in for debugging:
#Read-Host -Prompt "Press Enter to exit"

