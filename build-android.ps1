param([string]$AndroidSdk=$env:ANDROID_HOME,[string]$JavaRoot=$env:JAVA_HOME)
$ErrorActionPreference='Stop'
if(-not $AndroidSdk){$AndroidSdk=Join-Path $env:LOCALAPPDATA 'Android\Sdk'}
if(-not $JavaRoot){$JavaRoot=Join-Path $env:ProgramFiles 'Android\Android Studio\jbr'}
if(-not (Test-Path -LiteralPath $AndroidSdk)){throw 'Indica el SDK con -AndroidSdk o ANDROID_HOME.'}
if(-not (Test-Path -LiteralPath (Join-Path $JavaRoot 'bin\java.exe'))){throw 'Indica el JDK/JBR con -JavaRoot o JAVA_HOME.'}
$env:ANDROID_HOME=$AndroidSdk
$env:JAVA_HOME=$JavaRoot
$env:GRADLE_USER_HOME=Join-Path $PSScriptRoot '.build-cache\gradle'
Push-Location (Join-Path $PSScriptRoot 'android')
try {
    & .\gradlew.bat assembleDebug --console=plain
    if($LASTEXITCODE -ne 0){throw "Compilación fallida ($LASTEXITCODE)."}
    Write-Output (Join-Path $PSScriptRoot 'android\app\build\outputs\apk\debug\app-debug.apk')
} finally { Pop-Location }
