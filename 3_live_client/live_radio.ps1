# --- CONFIGURATION ---
$ServerIP = "127.0.0.1" 
$Port = 12345           
$Token = "ANTENNE"      
$MicrophoneName = "Headset (JBL Live 770NC)" # N'oublie pas de remettre ton vrai nom de micro ici !
# ---------------------

Write-Host "[INFO] Tentative de connexion au serveur Radio ($($ServerIP):$($Port))..." -ForegroundColor Cyan

try {
    # 1. Connexion TCP
    $TcpClient = New-Object System.Net.Sockets.TcpClient($ServerIP, $Port)
    $NetworkStream = $TcpClient.GetStream()
    Write-Host "[OK] Connecte au serveur !" -ForegroundColor Green

    # 2. Envoi du Token de securite
    Write-Host "[INFO] Envoi du mot de passe..." -ForegroundColor Yellow
    $TokenBytes = [System.Text.Encoding]::ASCII.GetBytes($Token)
    $NetworkStream.Write($TokenBytes, 0, $TokenBytes.Length)
    
    Start-Sleep -Milliseconds 500 

    # 3. Preparation de FFmpeg
    Write-Host "[INFO] Demarrage du microphone via FFmpeg..." -ForegroundColor Cyan
    $FFmpegInfo = New-Object System.Diagnostics.ProcessStartInfo
    $FFmpegInfo.FileName = ".\ffmpeg.exe" 
    $FFmpegInfo.Arguments = "-f dshow -i audio=`"$MicrophoneName`" -f s16le -acodec pcm_s16le -ar 44100 -ac 2 pipe:1"
    $FFmpegInfo.UseShellExecute = $false
    $FFmpegInfo.RedirectStandardOutput = $true
    $FFmpegInfo.RedirectStandardError = $false 
    $FFmpegInfo.CreateNoWindow = $true

    # 4. Lancement de FFmpeg
    $FFmpegProcess = [System.Diagnostics.Process]::Start($FFmpegInfo)
    $FFmpegOut = $FFmpegProcess.StandardOutput.BaseStream

    $Buffer = New-Object byte[] 4096
    
    Write-Host ""
    Write-Host "==========================================" -ForegroundColor Red
    Write-Host " [LIVE] VOUS ETES EN DIRECT SUR LA RADIO ! " -ForegroundColor Red
    Write-Host "==========================================" -ForegroundColor Red
    Write-Host "(Faites CTRL+C dans cette fenetre pour rendre l'antenne)" -ForegroundColor Yellow
    Write-Host ""

    while ($FFmpegProcess.HasExited -eq $false) {
        $BytesRead = $FFmpegOut.Read($Buffer, 0, $Buffer.Length)
        if ($BytesRead -gt 0) {
            $NetworkStream.Write($Buffer, 0, $BytesRead)
        }
    }
} catch {
    Write-Error "[ERREUR] Probleme de connexion ou de transmission : $_"
} finally {
    Write-Host "[INFO] Fin du direct. Nettoyage..." -ForegroundColor Yellow
    if ($NetworkStream) { $NetworkStream.Close() }
    if ($TcpClient) { $TcpClient.Close() }
    if ($FFmpegProcess -and -not $FFmpegProcess.HasExited) { 
        $FFmpegProcess.Kill() 
    }
    Write-Host "[OK] Antenne rendue avec succes. La playlist musicale reprend." -ForegroundColor Green
    
    Write-Host ""
    Read-Host "Appuyez sur Entree pour fermer cette fenetre..."
}