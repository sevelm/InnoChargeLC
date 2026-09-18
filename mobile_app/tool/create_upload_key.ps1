param([string]$Keytool = 'C:\Program Files\Android\Android Studio\jbr\bin\keytool.exe')
$ErrorActionPreference = 'Stop'
$android = Join-Path (Split-Path $PSScriptRoot -Parent) 'android'
$keystore = Join-Path $android 'upload-keystore.jks'
$properties = Join-Path $android 'key.properties'
if ((Test-Path -LiteralPath $keystore) -or (Test-Path -LiteralPath $properties)) {
    throw 'Signing files already exist. Reuse them; never replace an existing upload key here.'
}
if (-not (Test-Path -LiteralPath $Keytool)) { throw 'Java keytool not found. Pass -Keytool with its path.' }

# Generate secrets locally; never print them or put them in process arguments.
$bytes = New-Object byte[] 32
$random = [Security.Cryptography.RandomNumberGenerator]::Create()
try { $random.GetBytes($bytes) } finally { $random.Dispose() }
$password = [BitConverter]::ToString($bytes).Replace('-', '')
$env:INNOCHARGE_UPLOAD_PASSWORD = $password
try {
    # Persist the password first so it is recoverable if key generation is interrupted.
    $settings = "storePassword=$password`nkeyPassword=$password`nkeyAlias=upload`nstoreFile=upload-keystore.jks`n"
    [IO.File]::WriteAllText($properties, $settings, [Text.Encoding]::ASCII)
    & $Keytool -genkeypair -noprompt -keystore $keystore -storetype JKS -alias upload `
        -keyalg RSA -keysize 3072 -sigalg SHA256withRSA -validity 10000 `
        -dname 'CN=InnoCharge Upload' `
        -storepass:env INNOCHARGE_UPLOAD_PASSWORD -keypass:env INNOCHARGE_UPLOAD_PASSWORD
    if ($LASTEXITCODE -ne 0) { throw 'Upload key generation failed. Preserve key.properties and inspect the files before retrying.' }
    $owner = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $system = New-Object Security.Principal.SecurityIdentifier('S-1-5-18')
    foreach ($path in @($properties, $keystore)) {
        $acl = New-Object Security.AccessControl.FileSecurity
        $acl.SetOwner($owner)
        $acl.SetAccessRuleProtection($true, $false)
        $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($owner, 'FullControl', 'Allow')))
        $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule($system, 'FullControl', 'Allow')))
        Set-Acl -LiteralPath $path -AclObject $acl
    }
    Write-Output 'Upload key created. Back up android/upload-keystore.jks and android/key.properties securely.'
} finally {
    Remove-Item Env:INNOCHARGE_UPLOAD_PASSWORD -ErrorAction SilentlyContinue
    $password = $null
    $settings = $null
    [Array]::Clear($bytes, 0, $bytes.Length)
}
