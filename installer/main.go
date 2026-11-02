// Установщик бересты.
//
// Linux: проверяет инструменты, скачивает исходники, собирает компилятор и
// кладёт его в ~/.local/bin.
//
// Windows: использует WSL (родной бэкенд бересты — x86-64 System V/ELF, поэтому
// нативный PE пока не поддерживается). Ставит зависимости внутри WSL, собирает
// компилятор и создаёт обёртку berestac.bat.
package main

import (
	"archive/tar"
	"archive/zip"
	"bytes"
	"errors"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

var toolVersion = "0.15.0"

var (
	flagPrefix = flag.String("prefix", "", "куда установить (Linux; по умолчанию ~/.local)")
	flagRepo   = flag.String("repo", "", "URL zip-архива с исходниками")
	flagKeep   = flag.Bool("keep-src", false, "не удалять скачанные исходники")
	flagNoPath = flag.Bool("no-path", false, "не подсказывать про PATH")
	flagCheck  = flag.Bool("check", false, "только проверить инструменты")
	flagDeps   = flag.Bool("deps", true, "ставить недостающие зависимости, если можно")
	flagWSL    = flag.Bool("wsl", false, "на Windows поставить через WSL вместо нативного режима")
)

const defaultRepo = "https://codeload.github.com/xelira/Beresta/zip/refs/heads/main"

func main() {
	flag.Parse()

	fmt.Printf("установщик бересты %s (%s/%s)\n", toolVersion, runtime.GOOS, runtime.GOARCH)

	var err error
	switch runtime.GOOS {
	case "linux":
		err = installLinux()
	case "windows":
		err = installWindows()
	default:
		err = fmt.Errorf("поддерживаются Linux и Windows; на %s собирайте вручную: make", runtime.GOOS)
	}

	if err != nil {
		fmt.Fprintln(os.Stderr, "\nошибка:", err)
		os.Exit(1)
	}
}

// --- общее ---------------------------------------------------------------

func commandExists(name string) bool {
	_, err := exec.LookPath(name)
	return err == nil
}

func runInteractive(name string, args ...string) error {
	cmd := exec.Command(name, args...)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

func runQuiet(name string, args ...string) (string, error) {
	cmd := exec.Command(name, args...)
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &out
	err := cmd.Run()
	return strings.TrimSpace(out.String()), err
}

func download(url, destination string) error {
	client := &http.Client{Timeout: 5 * time.Minute}
	response, err := client.Get(url)
	if err != nil {
		return fmt.Errorf("не удалось скачать %s: %w", url, err)
	}
	defer response.Body.Close()

	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("сервер вернул %s для %s", response.Status, url)
	}

	file, err := os.Create(destination)
	if err != nil {
		return err
	}
	defer file.Close()

	_, err = io.Copy(file, response.Body)
	return err
}

// распаковывает zip в каталог, не выпуская пути наружу
func unzip(archive, destination string) error {
	reader, err := zip.OpenReader(archive)
	if err != nil {
		return err
	}
	defer reader.Close()

	for _, entry := range reader.File {
		target := filepath.Join(destination, entry.Name)
		if !strings.HasPrefix(target, filepath.Clean(destination)+string(os.PathSeparator)) {
			return fmt.Errorf("подозрительный путь в архиве: %s", entry.Name)
		}

		if entry.FileInfo().IsDir() {
			if err := os.MkdirAll(target, 0o755); err != nil {
				return err
			}
			continue
		}

		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return err
		}
		source, err := entry.Open()
		if err != nil {
			return err
		}
		data, err := io.ReadAll(source)
		source.Close()
		if err != nil {
			return err
		}
		if err := os.WriteFile(target, data, 0o644); err != nil {
			return err
		}
	}

	return nil
}

func sourceRoot(directory string) (string, error) {
	if _, err := os.Stat(filepath.Join(directory, "src", "codegen.cpp")); err == nil {
		return directory, nil
	}

	entries, err := os.ReadDir(directory)
	if err != nil {
		return "", err
	}
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		candidate := filepath.Join(directory, entry.Name())
		if _, err := os.Stat(filepath.Join(candidate, "src", "codegen.cpp")); err == nil {
			return candidate, nil
		}
	}

	return "", errors.New("в архиве не найден src/codegen.cpp")
}

//: Локальный репозиторий или скачанный архив.
func obtainSource() (string, func(), error) {
	if _, err := os.Stat(filepath.Join("src", "codegen.cpp")); err == nil {
		absolute, err := filepath.Abs(".")
		return absolute, func() {}, err
	}

	cache, err := os.UserCacheDir()
	if err != nil {
		cache = os.TempDir()
	}
	work := filepath.Join(cache, "beresta-install")
	if err := os.RemoveAll(work); err != nil {
		return "", func() {}, err
	}
	if err := os.MkdirAll(work, 0o755); err != nil {
		return "", func() {}, err
	}

	url := *flagRepo
	if url == "" {
		url = defaultRepo
	}
	archive := filepath.Join(work, "beresta.zip")

	fmt.Println("скачиваю исходники:", url)
	if err := download(url, archive); err != nil {
		return "", func() {}, err
	}

	fmt.Println("распаковываю…")
	if err := unzip(archive, work); err != nil {
		return "", func() {}, err
	}

	root, err := sourceRoot(work)
	if err != nil {
		return "", func() {}, err
	}

	cleanup := func() {}
	if !*flagKeep {
		cleanup = func() { os.RemoveAll(work) }
	}

	return root, cleanup, nil
}

func sourceFiles(root string) ([]string, error) {
	files, err := filepath.Glob(filepath.Join(root, "src", "*.cpp"))
	if err != nil {
		return nil, err
	}
	if len(files) == 0 {
		return nil, errors.New("не найдено ни одного .cpp в src/")
	}
	return files, nil
}

func buildCompiler(root, output string) error {
	files, err := sourceFiles(root)
	if err != nil {
		return err
	}

	compiler, err := pickCxx()
	if err != nil {
		return err
	}

	args := append([]string{"-std=c++20", "-O2", "-o", output}, files...)
	fmt.Printf("собираю: %s -std=c++20 -O2 src/*.cpp -o %s\n", compiler, output)
	return runInteractive(compiler, args...)
}

func pickCxx() (string, error) {
	for _, candidate := range []string{"g++", "c++", "clang++"} {
		if commandExists(candidate) {
			return candidate, nil
		}
	}
	return "", errors.New("не найден компилятор C++ (g++ / clang++)")
}

func copyFile(source, destination string, mode os.FileMode) error {
	data, err := os.ReadFile(source)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(destination), 0o755); err != nil {
		return err
	}
	if err := os.WriteFile(destination, data, mode); err != nil {
		return err
	}
	return os.Chmod(destination, mode)
}

// --- Linux ---------------------------------------------------------------

func installLinux() error {
	if err := checkLinuxTools(); err != nil {
		return err
	}
	if *flagCheck {
		return nil
	}

	root, cleanup, err := obtainSource()
	if err != nil {
		return err
	}
	defer cleanup()

	prefix := *flagPrefix
	if prefix == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return err
		}
		prefix = filepath.Join(home, ".local")
	}

	temporary, err := os.MkdirTemp("", "beresta-build-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(temporary)

	binary := filepath.Join(temporary, "berestac")
	if err := buildCompiler(root, binary); err != nil {
		return fmt.Errorf("сборка не удалась: %w", err)
	}

	target := filepath.Join(prefix, "bin", "berestac")
	if err := copyFile(binary, target, 0o755); err != nil {
		return err
	}

	fmt.Println("\nготово:", target)
	if !*flagNoPath {
		printPathHint(filepath.Join(prefix, "bin"))
	}

	return runInteractive(target, "version")
}

func checkLinuxTools() error {
	missing := []string{}
	for _, tool := range []string{"g++", "as", "cc"} {
		if !commandExists(tool) {
			missing = append(missing, tool)
		}
	}

	if len(missing) == 0 {
		fmt.Println("инструменты на месте: g++, as, cc")
		return nil
	}

	fmt.Println("не хватает:", strings.Join(missing, ", "))
	if !*flagDeps {
		return errors.New("установите недостающие инструменты и повторите")
	}

	if isAdmin() {
		if err := installSystemDeps(); err != nil {
			return err
		}
		return checkLinuxTools()
	}

	fmt.Println("\nпоставьте их вручную:")
	fmt.Println("  Debian/Ubuntu:  sudo apt install g++ binutils")
	fmt.Println("  Fedora:         sudo dnf install gcc-c++ binutils")
	fmt.Println("  Arch:           sudo pacman -S gcc binutils")
	fmt.Println("затем запустите установщик снова")
	return errors.New("не хватает инструментов сборки")
}

func installSystemDeps() error {
	switch {
	case commandExists("apt-get"):
		return runInteractive("apt-get", "install", "-y", "g++", "binutils")
	case commandExists("dnf"):
		return runInteractive("dnf", "install", "-y", "gcc-c++", "binutils")
	case commandExists("pacman"):
		return runInteractive("pacman", "-S", "--noconfirm", "gcc", "binutils")
	}
	return errors.New("неизвестный пакетный менеджер; поставьте g++ и binutils вручную")
}

func printPathHint(directory string) {
	path := os.Getenv("PATH")
	for _, part := range filepath.SplitList(path) {
		if part == directory {
			return
		}
	}

	fmt.Println("\nдобавьте в PATH:")
	fmt.Printf("  export PATH=\"%s:$PATH\"\n", directory)
	fmt.Println("или вставьте эту строку в ~/.profile")
}

// --- Windows (через WSL) -------------------------------------------------

func installWindows() error {
	if !*flagWSL {
		if err := installWindowsNative(); err == nil {
			return nil
		} else {
			fmt.Println("нативная установка не удалась:", err)
			fmt.Println("пробую через WSL…")
		}
	}
	return installWindowsWSL()
}

func installWindowsWSL() error {
	if !commandExists("wsl.exe") {
		return errors.New("не найден WSL. Установите его:\n" +
			"  wsl --install\n" +
			"перезагрузитесь, затем запустите установщик снова")
	}

	distros, err := runQuiet("wsl.exe", "-l", "-q")
	if err != nil || strings.TrimSpace(distros) == "" {
		return errors.New("WSL есть, но дистрибутив не установлен:\n  wsl --install -d Ubuntu")
	}
	fmt.Println("WSL найден:", strings.ReplaceAll(strings.TrimSpace(distros), "\x00", ""))

	if err := ensureWSLDeps(); err != nil {
		return err
	}
	if *flagCheck {
		return nil
	}

	root, cleanup, err := obtainSource()
	if err != nil {
		return err
	}
	defer cleanup()

	fmt.Println("собираю внутри WSL…")
	build := "set -e; rm -rf ~/.beresta/src; mkdir -p ~/.beresta/src ~/.beresta/bin; " +
		"cd ~/.beresta/src && tar -xzf - && " +
		"g++ -std=c++20 -O2 src/*.cpp -o ~/.beresta/bin/berestac && " +
		"chmod +x ~/.beresta/bin/berestac && ~/.beresta/bin/berestac version"

	cmd := exec.Command("wsl.exe", "-e", "bash", "-lc", build)
	cmd.Stdin = sourceTar(root)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("сборка в WSL не удалась: %w", err)
	}

	home, err := runQuiet("wsl.exe", "-e", "bash", "-lc", "echo $HOME")
	if err != nil {
		return err
	}
	home = strings.TrimSpace(strings.ReplaceAll(home, "\x00", ""))

	if err := installWindowsShim(filepath.Join(home, ".beresta", "bin", "berestac")); err != nil {
		return err
	}

	fmt.Println("\nготово. Откройте новый терминал и проверьте: berestac version")
	return nil
}

func ensureWSLDeps() error {
	script := "command -v g++ >/dev/null && command -v as >/dev/null && command -v cc >/dev/null"
	if _, err := runQuiet("wsl.exe", "-e", "bash", "-lc", script); err == nil {
		fmt.Println("инструменты в WSL на месте")
		return nil
	}

	if !*flagDeps {
		return errors.New("в WSL нет g++/binutils: sudo apt install g++ binutils")
	}

	fmt.Println("ставлю g++ и binutils внутри WSL…")
	err := runInteractive("wsl.exe", "-u", "root", "-e", "bash", "-lc",
		"apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y g++ binutils")
	if err != nil {
		return fmt.Errorf("не удалось поставить зависимости в WSL: %w", err)
	}
	return nil
}

//: Отдаёт tar-поток с исходниками для распаковки внутри WSL.
func sourceTar(root string) io.Reader {
	reader, writer := io.Pipe()

	go func() {
		defer writer.Close()
		archive := tar.NewWriter(writer)
		defer archive.Close()

		_ = filepath.Walk(filepath.Join(root, "src"), func(path string, info os.FileInfo, err error) error {
			if err != nil || info.IsDir() {
				return nil
			}

			relative, err := filepath.Rel(root, path)
			if err != nil {
				return nil
			}

			header, err := tar.FileInfoHeader(info, "")
			if err != nil {
				return nil
			}
			header.Name = filepath.ToSlash(relative)

			if err := archive.WriteHeader(header); err != nil {
				return nil
			}
			file, err := os.Open(path)
			if err != nil {
				return nil
			}
			defer file.Close()
			_, _ = io.Copy(archive, file)
			return nil
		})
	}()

	return reader
}



func installWindowsShim(binaryPath string) error {
	local := os.Getenv("LOCALAPPDATA")
	if local == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return err
		}
		local = home
	}

	shimDir := filepath.Join(local, "beresta", "bin")
	if err := os.MkdirAll(shimDir, 0o755); err != nil {
		return err
	}

	shim := filepath.Join(shimDir, "berestac.bat")
	content := "@echo off\r\n" +
		"wsl.exe -e " + binaryPath + " %*\r\n" +
		"exit /b %ERRORLEVEL%\r\n"

	if err := os.WriteFile(shim, []byte(content), 0o644); err != nil {
		return err
	}
	fmt.Println("обёртка:", shim)

	if *flagNoPath {
		return nil
	}

	script := fmt.Sprintf(
		"$p=[Environment]::GetEnvironmentVariable('Path','User'); "+
			"if($p -notlike '*beresta\\bin*'){[Environment]::SetEnvironmentVariable('Path',$p+';%s','User')}",
		strings.ReplaceAll(shimDir, "'", "''"))

	if _, err := runQuiet("powershell.exe", "-NoProfile", "-Command", script); err != nil {
		fmt.Println("не удалось обновить PATH автоматически. Добавьте вручную:")
		fmt.Println(" ", shimDir)
	}

	return nil
}
