package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

const releaseBase = "https://github.com/xelira/Beresta/releases/latest/download/"
const winlibsAPI = "https://api.github.com/repos/brechtsanders/winlibs_mingw/releases/latest"

func installWindowsNative() error {
	local := os.Getenv("LOCALAPPDATA")
	if local == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return err
		}
		local = home
	}

	root := filepath.Join(local, "beresta")
	binDir := filepath.Join(root, "bin")
	toolchainDir := filepath.Join(root, "toolchain")

	if err := os.MkdirAll(binDir, 0o755); err != nil {
		return err
	}
	if err := os.MkdirAll(toolchainDir, 0o755); err != nil {
		return err
	}

	fmt.Println("скачиваю компилятор…")
	compiler := filepath.Join(binDir, "berestac.exe")
	if err := downloadTo(releaseBase+"berestac-windows-amd64.exe", compiler); err != nil {
		return fmt.Errorf("не удалось скачать компилятор: %w", err)
	}

	fmt.Println("скачиваю лаунчер…")
	launcher := filepath.Join(binDir, "beresta.exe")
	if err := downloadTo(releaseBase+"beresta-windows-amd64.exe", launcher); err != nil {
		return fmt.Errorf("не удалось скачать лаунчер: %w", err)
	}

	fmt.Println("ищу MinGW-w64 (ассемблер и линкер)…")
	toolchainBin, err := ensureWinlibs(toolchainDir)
	if err != nil {
		return err
	}

	fmt.Println("обновляю PATH…")
	if err := addToUserPath(binDir, toolchainBin); err != nil {
		fmt.Println("  не удалось обновить PATH автоматически:", err)
		fmt.Println("  добавьте вручную:", binDir+";"+toolchainBin)
	}

	fmt.Println("\nготово.")
	fmt.Println("  компилятор:", compiler)
	fmt.Println("  лаунчер:  ", launcher)
	fmt.Println("  MinGW:    ", toolchainBin)
	fmt.Println("\nОткройте новый терминал и проверьте: beresta version")
	return nil
}

func downloadTo(url, destination string) error {
	client := &http.Client{Timeout: 10 * time.Minute}
	response, err := client.Get(url)
	if err != nil {
		return err
	}
	defer response.Body.Close()

	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("сервер вернул %s", response.Status)
	}

	file, err := os.Create(destination)
	if err != nil {
		return err
	}
	defer file.Close()

	_, err = io.Copy(file, response.Body)
	return err
}

type githubRelease struct {
	Assets []struct {
		Name string `json:"name"`
		URL  string `json:"browser_download_url"`
	} `json:"assets"`
}

func ensureWinlibs(directory string) (string, error) {
	for _, candidate := range []string{"mingw64", "mingw32"} {
		bin := filepath.Join(directory, candidate, "bin")
		if _, err := os.Stat(filepath.Join(bin, "g++.exe")); err == nil {
			return bin, nil
		}
	}

	url, err := winlibsURL()
	if err != nil {
		return "", err
	}

	fmt.Println("скачиваю MinGW-w64 (это может занять пару минут)…")
	archive := filepath.Join(os.TempDir(), "beresta-winlibs.zip")
	if err := downloadTo(url, archive); err != nil {
		return "", err
	}
	defer os.Remove(archive)

	if err := unzip(archive, directory); err != nil {
		return "", err
	}

	for _, candidate := range []string{"mingw64", "mingw32"} {
		bin := filepath.Join(directory, candidate, "bin")
		if _, err := os.Stat(filepath.Join(bin, "g++.exe")); err == nil {
			return bin, nil
		}
	}
	return "", errors.New("в архиве MinGW не найден g++.exe")
}

func winlibsURL() (string, error) {
	request, err := http.NewRequest("GET", winlibsAPI, nil)
	if err != nil {
		return "", err
	}
	request.Header.Set("User-Agent", "beresta-installer")

	client := &http.Client{Timeout: 2 * time.Minute}
	response, err := client.Do(request)
	if err != nil {
		return "", err
	}
	defer response.Body.Close()

	if response.StatusCode != http.StatusOK {
		return "", fmt.Errorf("GitHub API вернул %s", response.Status)
	}

	var release githubRelease
	if err := json.NewDecoder(response.Body).Decode(&release); err != nil {
		return "", err
	}

	for _, asset := range release.Assets {
		name := strings.ToLower(asset.Name)
		if strings.HasSuffix(name, ".zip") &&
			strings.Contains(name, "x86_64") &&
			strings.Contains(name, "posix-seh") &&
			!strings.Contains(name, "llvm") {
			return asset.URL, nil
		}
	}

	return "", errors.New("не найден подходящий zip MinGW-w64 в последнем релизе winlibs")
}

func addToUserPath(directories ...string) error {
	script := fmt.Sprintf(
		"$p=[Environment]::GetEnvironmentVariable('Path','User'); "+
			"if(-not $p){$p=''}; "+
			"foreach($d in @(%s)){ if($p -notlike ('*'+$d+'*')){ $p = $p + ';' + $d } }; "+
			"[Environment]::SetEnvironmentVariable('Path',$p,'User')",
		quotePowerShellList(directories))

	cmd := exec.Command("powershell.exe", "-NoProfile", "-Command", script)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("%s: %w", strings.TrimSpace(string(output)), err)
	}
	return nil
}

func quotePowerShellList(values []string) string {
	parts := make([]string, 0, len(values))
	for _, value := range values {
		parts = append(parts, "'"+strings.ReplaceAll(value, "'", "''")+"'")
	}
	return strings.Join(parts, ",")
}
