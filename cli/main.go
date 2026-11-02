// beresta — простой консольный лаунчер.
//
//   beresta run <файл.бер | папка>
//   beresta build <файл.бер | папка> [-o выход]
//   beresta check <файл.бер | папка>
//   beresta new <папка>
//   beresta doctor
//
// На Linux запускает berestac напрямую, на Windows — через WSL.
package main

import (
	"bufio"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
)

const version = "0.15.0"

func main() {
	arguments := os.Args[1:]
	if len(arguments) == 0 {
		usage()
		os.Exit(2)
	}

	command := arguments[0]
	rest := arguments[1:]

	var code int
	switch command {
	case "run":
		code = commandRun(rest)
	case "build":
		code = commandBuild(rest)
	case "check":
		code = commandCheck(rest)
	case "new":
		code = commandNew(rest)
	case "doctor":
		code = commandDoctor()
	case "version", "-v", "--version":
		fmt.Println("береста", version)
	case "help", "-h", "--help":
		usage()
	default:
		// Короткая форма: beresta файл.бер
		code = commandRun(arguments)
	}

	os.Exit(code)
}

func usage() {
	fmt.Print(`береста ` + version + ` — консольный запуск языка береста

использование:
  beresta run <файл.бер | папка> [-- аргументы программы]
  beresta build <файл.бер | папка> [-o выходной файл]
  beresta check <файл.бер | папка>
  beresta new <папка>
  beresta doctor
  beresta version

примеры:
  beresta new моя_программа
  beresta run моя_программа
  beresta run пример.бер
  beresta build пример.бер -o пример

В папке лаунчер ищет точку входа: главная.бер, main.бер, береста.бер
или единственный файл с блоком «береста».
`)
}

// --- команды ---------------------------------------------------------------

func commandRun(arguments []string) int {
	target, programArgs, err := splitTarget(arguments)
	if err != nil {
		return fail(err)
	}

	entry, err := resolveEntry(target)
	if err != nil {
		return fail(err)
	}

	compiler, err := findCompiler()
	if err != nil {
		return fail(err)
	}

	output := temporaryBinary(entry, compiler.wsl)
	fmt.Println("береста: собираю «" + entry + "»")

	if code := invoke(compiler, []string{"build", entry, "-o", output}, false); code != 0 {
		return code
	}

	fmt.Println("береста: запускаю")
	code := invoke(toolchain{path: output}, programArgs, true)

	removeQuietly(output, compiler.wsl)
	return code
}

func commandBuild(arguments []string) int {
	target := ""
	output := ""

	for i := 0; i < len(arguments); i++ {
		switch arguments[i] {
		case "-o", "--output":
			if i+1 >= len(arguments) {
				return fail(errors.New("после «-o» нужен путь"))
			}
			output = arguments[i+1]
			i++
		default:
			if target == "" {
				target = arguments[i]
			}
		}
	}

	if target == "" {
		return fail(errors.New("не указан файл или папка"))
	}

	entry, err := resolveEntry(target)
	if err != nil {
		return fail(err)
	}

	compiler, err := findCompiler()
	if err != nil {
		return fail(err)
	}

	if output == "" {
		output = strings.TrimSuffix(entry, filepath.Ext(entry))
	}

	fmt.Println("береста: собираю «" + entry + "» в «" + output + "»")
	code := invoke(compiler, []string{"build", entry, "-o", output}, false)
	if code == 0 {
		fmt.Println("береста: готово:", output)
	}
	return code
}

func commandCheck(arguments []string) int {
	if len(arguments) == 0 {
		return fail(errors.New("не указан файл или папка"))
	}

	entry, err := resolveEntry(arguments[0])
	if err != nil {
		return fail(err)
	}

	compiler, err := findCompiler()
	if err != nil {
		return fail(err)
	}

	return invoke(compiler, []string{"check", entry}, false)
}

func commandNew(arguments []string) int {
	if len(arguments) == 0 {
		return fail(errors.New("не указана папка"))
	}

	directory := arguments[0]
	if err := os.MkdirAll(directory, 0o755); err != nil {
		return fail(err)
	}

	file := filepath.Join(directory, "главная.бер")
	if _, err := os.Stat(file); err == nil {
		return fail(fmt.Errorf("файл «%s» уже существует", file))
	}

	text := `// Новая программа на бересте.

береста {
    молви "Здравствуй, береста!";
}
`
	if err := os.WriteFile(file, []byte(text), 0o644); err != nil {
		return fail(err)
	}

	fmt.Println("береста: создано", file)
	fmt.Println("запуск: beresta run", directory)
	return 0
}

func commandDoctor() int {
	fmt.Println("береста", version)
	fmt.Println("система:", runtime.GOOS+"/"+runtime.GOARCH)

	compiler, err := findCompiler()
	if err != nil {
		fmt.Println("компилятор: не найден")
	} else {
		mode := "нативный"
		if compiler.wsl {
			mode = "через WSL"
		}
		fmt.Println("компилятор:", compiler.path, "("+mode+")")
	}

	if runtime.GOOS == "windows" && compiler.wsl {
		check := exec.Command("wsl.exe", "-e", "bash", "-lc",
			"command -v berestac; command -v as; command -v cc")
		output, err := check.CombinedOutput()
		if err != nil {
			fmt.Println("WSL: недоступен или не настроен")
		} else {
			fmt.Println("в WSL:")
			fmt.Print(string(output))
		}
		return 0
	}

	for _, tool := range []string{"as", "cc", "g++"} {
		if path, err := exec.LookPath(tool); err == nil {
			fmt.Println(tool+":", path)
		} else {
			fmt.Println(tool + ": не найден")
		}
	}
	return 0
}

// --- поиск точки входа ------------------------------------------------------

func resolveEntry(target string) (string, error) {
	info, err := os.Stat(target)
	if err != nil {
		return "", fmt.Errorf("не найдено: %s", target)
	}

	if !info.IsDir() {
		return target, nil
	}

	preferred := []string{"главная.бер", "main.бер", "береста.бер"}
	for _, name := range preferred {
		candidate := filepath.Join(target, name)
		if _, err := os.Stat(candidate); err == nil {
			return candidate, nil
		}
	}

	files, err := filepath.Glob(filepath.Join(target, "*.бер"))
	if err != nil {
		return "", err
	}

	var entries []string
	for _, file := range files {
		if hasEntryBlock(file) {
			entries = append(entries, file)
		}
	}

	if len(entries) == 1 {
		return entries[0], nil
	}
	if len(files) == 1 {
		return files[0], nil
	}
	if len(entries) > 1 {
		return "", fmt.Errorf("в папке несколько точек входа: %s", strings.Join(entries, ", "))
	}

	return "", fmt.Errorf("в папке «%s» не найдена точка входа", target)
}

func hasEntryBlock(file string) bool {
	handle, err := os.Open(file)
	if err != nil {
		return false
	}
	defer handle.Close()

	scanner := bufio.NewScanner(handle)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if strings.HasPrefix(line, "береста") {
			return true
		}
	}
	return false
}

// --- запуск компилятора и программы ----------------------------------------

type toolchain struct {
	path string
	wsl  bool
}

func findCompiler() (toolchain, error) {
	if len(embeddedCompiler) > 0 {
		if path, err := extractEmbeddedCompiler(); err == nil {
			return toolchain{path: path}, nil
		}
	}

	if value := os.Getenv("BERESTAC"); value != "" {
		return toolchain{path: value}, nil
	}

	executable, err := os.Executable()
	if err == nil {
		directory := filepath.Dir(executable)
		for _, name := range []string{"berestac", "berestac.exe"} {
			candidate := filepath.Join(directory, name)
			if _, err := os.Stat(candidate); err == nil {
				return toolchain{path: candidate}, nil
			}
		}
	}

	if path, err := exec.LookPath("berestac"); err == nil {
		return toolchain{path: path}, nil
	}

	if runtime.GOOS == "windows" {
		// Нативного компилятора нет — остаётся WSL.
		return toolchain{path: "berestac", wsl: true}, nil
	}

	return toolchain{}, errors.New("не найден berestac. Запустите установщик или укажите BERESTAC")
}

func extractEmbeddedCompiler() (string, error) {
	cache, err := os.UserCacheDir()
	if err != nil {
		return "", err
	}

	directory := filepath.Join(cache, "beresta", "bin")
	if err := os.MkdirAll(directory, 0o755); err != nil {
		return "", err
	}

	path := filepath.Join(directory, "berestac")
	if info, err := os.Stat(path); err == nil && info.Size() == int64(len(embeddedCompiler)) {
		return path, nil
	}

	if err := os.WriteFile(path, embeddedCompiler, 0o755); err != nil {
		return "", err
	}
	return path, nil
}

func invoke(tool toolchain, arguments []string, interactive bool) int {
	command := buildCommand(tool, arguments)
	command.Stdin = os.Stdin
	command.Stdout = os.Stdout
	command.Stderr = os.Stderr

	if !interactive {
		command.Stdin = nil
	}

	err := command.Run()
	if err == nil {
		return 0
	}

	var exitError *exec.ExitError
	if errors.As(err, &exitError) {
		return exitError.ExitCode()
	}

	fail(err)
	return 1
}

func buildCommand(tool toolchain, arguments []string) *exec.Cmd {
	if !tool.wsl {
		return exec.Command(tool.path, arguments...)
	}

	// Режим WSL: компилятор и пути — в Linux-представлении.
	converted := make([]string, 0, len(arguments))
	for _, argument := range arguments {
		if looksLikePath(argument) {
			converted = append(converted, toWSLPath(argument))
		} else {
			converted = append(converted, argument)
		}
	}

	return exec.Command("wsl.exe", append([]string{"-e", tool.path}, converted...)...)
}

func looksLikePath(value string) bool {
	if strings.HasPrefix(value, "-") {
		return false
	}
	if strings.ContainsAny(value, `/\`) || strings.HasSuffix(value, ".бер") {
		return true
	}
	return false
}

func toWSLPath(path string) string {
	absolute, err := filepath.Abs(path)
	if err != nil {
		return path
	}

	command := exec.Command("wsl.exe", "-e", "wslpath", "-a", absolute)
	output, err := command.Output()
	if err != nil {
		return path
	}
	return strings.TrimSpace(string(output))
}

func temporaryBinary(entry string, wsl bool) string {
	name := strings.TrimSuffix(filepath.Base(entry), filepath.Ext(entry))
	if wsl {
		return "/tmp/beresta-run-" + strconv.Itoa(os.Getpid()) + "-" + name
	}
	if runtime.GOOS == "windows" {
		name += ".exe"
	}

	directory, err := os.MkdirTemp("", "beresta-run-")
	if err != nil {
		directory = os.TempDir()
	}
	return filepath.Join(directory, name)
}

func removeQuietly(path string, wsl bool) {
	if wsl {
		exec.Command("wsl.exe", "-e", "rm", "-rf", path).Run()
		return
	}
	os.RemoveAll(filepath.Dir(path))
}

// --- служебное -------------------------------------------------------------

func splitTarget(arguments []string) (string, []string, error) {
	if len(arguments) == 0 {
		return "", nil, errors.New("не указан файл или папка")
	}

	target := ""
	var programArgs []string

	for i := 0; i < len(arguments); i++ {
		if arguments[i] == "--" {
			programArgs = append(programArgs, arguments[i+1:]...)
			break
		}
		if target == "" {
			target = arguments[i]
		} else {
			programArgs = append(programArgs, arguments[i])
		}
	}

	if target == "" {
		return "", nil, errors.New("не указан файл или папка")
	}

	return target, programArgs, nil
}

func fail(err error) int {
	fmt.Fprintln(os.Stderr, "береста:", err)
	return 1
}
