// SPDX-License-Identifier: GPL-3.0-only
package launcher

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	assets "pdf-editor"
	"runtime"
	"strconv"
	"strings"
	"time"
)

const DefaultImage = "ewave267/pdf-editor:0.1.0"
const ownerLabel = "org.pdf-editor.launcher"

type options struct {
	imageSet, portSet                 bool
	action, image, name, home, source string
	port, jobs                        int
	open, root, emulation             bool
}
type container struct {
	State struct {
		Status string
		Health *struct{ Status string }
	}
	Config struct {
		Image  string
		Labels map[string]string
	}
	Mounts          []struct{ Source, Destination string }
	NetworkSettings struct {
		Ports map[string][]struct{ HostIP, HostPort string }
	}
}
type runner func(context.Context, bool, ...string) (string, error)
type app struct {
	call     runner
	out      io.Writer
	errOut   io.Writer
	cache    string
	goos     string
	uid, gid int
}

func parse(args []string, out io.Writer) (options, error) {
	o := options{action: "start"}
	if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		o.action = args[0]
		args = args[1:]
	}
	f := flag.NewFlagSet("pdf-editor "+o.action, flag.ContinueOnError)
	f.SetOutput(out)
	f.StringVar(&o.image, "image", DefaultImage, "versioned Docker image")
	f.StringVar(&o.name, "name", "pdf-editor", "container name")
	f.StringVar(&o.home, "home", "", "host folder to share (default: your home)")
	f.IntVar(&o.port, "port", 8080, "localhost browser port; 0 chooses an available port")
	f.IntVar(&o.jobs, "jobs", 4, "source build jobs (1-8)")
	f.StringVar(&o.source, "source", "", "source checkout containing Dockerfile (build command)")
	f.BoolVar(&o.open, "open", false, "also open your default browser")
	f.BoolVar(&o.root, "host-root", false, "share host root instead (local Linux only)")
	f.BoolVar(&o.emulation, "allow-emulation", false, "try the amd64 image on an ARM Docker engine (experimental)")
	f.Usage = func() {
		fmt.Fprintln(out, "Usage: pdf-editor [start|stop|status|logs|url|update|build|version|license] [options]\n\nStart pulls the image if needed, creates the container once and prints its URL.\nUpdate replaces the session: save edits first. Existing start preserves configuration.\nUsers need Docker, but no Go, Qt, Python, compiler or source checkout.")
		f.PrintDefaults()
	}
	if err := f.Parse(args); err != nil {
		return o, err
	}
	f.Visit(func(option *flag.Flag) {
		if option.Name == "image" {
			o.imageSet = true
		}
		if option.Name == "port" {
			o.portSet = true
		}
	})
	if f.NArg() != 0 {
		return o, fmt.Errorf("unexpected arguments: %s", strings.Join(f.Args(), " "))
	}
	switch o.action {
	case "start", "stop", "status", "logs", "url", "update", "build", "version", "license":
	default:
		return o, fmt.Errorf("unknown command %q", o.action)
	}
	if o.port < 0 || o.port > 65535 || o.jobs < 1 || o.jobs > 8 {
		return o, errors.New("port must be 0-65535 and jobs must be 1-8")
	}
	if o.name == "" || strings.HasPrefix(o.name, "-") || o.image == "" || strings.HasPrefix(o.image, "-") {
		return o, errors.New("invalid container name or image")
	}
	if o.root && o.home != "" {
		return o, errors.New("choose --home or --host-root, not both")
	}
	return o, nil
}

func Run(ctx context.Context, args []string, out, errOut io.Writer) error {
	o, err := parse(args, out)
	if errors.Is(err, flag.ErrHelp) {
		return nil
	}
	if err != nil {
		return err
	}
	if o.action == "version" {
		fmt.Fprintln(out, "PDF Editor launcher 0.1.0")
		return nil
	}
	if o.action == "license" {
		for _, p := range []string{"LICENSE", "packaging/docker/MOBY-PROFILES-LICENSE"} {
			b, e := assets.Files.ReadFile(p)
			if e != nil {
				return e
			}
			fmt.Fprintf(out, "%s\n%s\n", p, b)
		}
		return nil
	}
	binary, err := exec.LookPath("docker")
	if err != nil {
		return errors.New("install Docker Engine (Linux) or Docker Desktop (Windows/macOS), then run again")
	}
	cache, err := os.UserCacheDir()
	if err != nil {
		return err
	}
	a := app{out: out, errOut: errOut, cache: filepath.Join(cache, "pdf-editor-launcher"), goos: runtime.GOOS, uid: os.Getuid(), gid: os.Getgid()}
	a.call = func(ctx context.Context, stream bool, args ...string) (string, error) {
		cmd := exec.CommandContext(ctx, binary, args...)
		if stream {
			cmd.Stdout = out
			cmd.Stderr = errOut
			return "", cmd.Run()
		}
		b, e := cmd.CombinedOutput()
		if e != nil {
			return "", fmt.Errorf("docker %s: %w\n%s", strings.Join(args, " "), e, b)
		}
		return strings.TrimSpace(string(b)), nil
	}
	return a.execute(ctx, o)
}

func (a *app) inspect(ctx context.Context, name string) (*container, error) {
	b, err := a.call(ctx, false, "container", "inspect", name)
	if err != nil {
		// Distinguish absence from daemon/permission failures before creating anything.
		if strings.Contains(err.Error(), "No such container") || strings.Contains(err.Error(), "No such object") {
			return nil, nil
		}
		return nil, err
	}
	var list []container
	if err = json.Unmarshal([]byte(b), &list); err != nil {
		return nil, err
	}
	if len(list) != 1 {
		return nil, errors.New("unexpected container inspection response")
	}
	return &list[0], nil
}
func (a *app) execute(ctx context.Context, o options) error {
	var info struct {
		OSType, Architecture string
		SecurityOptions      []string
	}
	b, err := a.call(ctx, false, "info", "--format", "{{json .}}")
	if err != nil {
		return fmt.Errorf("Docker is unavailable; start it and check your access: %w", err)
	}
	if err = json.Unmarshal([]byte(b), &info); err != nil {
		return err
	}
	if info.OSType != "linux" {
		return errors.New("select Linux containers in Docker Desktop; this editor image is Linux")
	}
	if o.action == "build" {
		if info.Architecture != "x86_64" && info.Architecture != "amd64" && !o.emulation {
			return errors.New("amd64 source builds on ARM require experimental --allow-emulation")
		}
		if o.source == "" {
			return errors.New("build requires --source /path/to/checkout")
		}
		if strings.Contains(o.image, "@") {
			return errors.New("source builds require an image tag rather than a digest")
		}
		source, e := filepath.Abs(o.source)
		if e != nil {
			return e
		}
		if _, e = os.Stat(filepath.Join(source, "Dockerfile")); e != nil {
			return e
		}
		fmt.Fprintln(a.out, "Building inside Docker; the first PDFium/V8 build can take a long time.")
		_, e = a.call(ctx, true, "build", "--platform", "linux/amd64", "--build-arg", "BUILD_JOBS="+strconv.Itoa(o.jobs), "--tag", o.image, source)
		return e
	}
	c, err := a.inspect(ctx, o.name)
	if err != nil {
		return err
	}
	if c != nil && c.Config.Labels[ownerLabel] != "1" && c.Config.Labels["com.docker.compose.service"] != "pdf-editor" {
		return errors.New("that name belongs to another application; choose --name")
	}
	if (o.action == "update" || (o.action == "start" && c == nil)) && info.Architecture != "x86_64" && info.Architecture != "amd64" && !o.emulation {
		return errors.New("only the linux/amd64 image is available; ARM emulation is unvalidated (opt in with --allow-emulation)")
	}
	switch o.action {
	case "stop":
		if c == nil {
			fmt.Fprintln(a.out, "Already stopped: no container exists.")
			return nil
		}
		_, err = a.call(ctx, true, "stop", o.name)
		return err
	case "logs":
		if c == nil {
			return errors.New("no container exists; run start first")
		}
		_, err = a.call(ctx, true, "logs", "--tail", "100", o.name)
		return err
	case "status":
		if c == nil {
			fmt.Fprintln(a.out, "Not created. Run pdf-editor start.")
			return nil
		}
		fmt.Fprintf(a.out, "%s: %s\n", o.name, c.State.Status)
		if c.State.Status != "running" {
			return nil
		}
		return a.printURL(c, o.open)
	case "url":
		if c == nil || c.State.Status != "running" {
			return errors.New("run start first to obtain the browser URL")
		}
		return a.printURL(c, o.open)
	}
	if c != nil && c.Config.Labels[ownerLabel] != "1" && c.Config.Labels["com.docker.compose.service"] != "pdf-editor" {
		return errors.New("that name belongs to another application; choose --name")
	}
	if o.action == "update" {
		if c != nil && c.Config.Labels[ownerLabel] != "1" {
			return errors.New("existing container is managed by Compose; use ./docker.sh rebuild or choose a separate --name")
		}
		fmt.Fprintln(a.out, "Updating replaces the desktop session; unsaved edits are lost. Host files remain.")
		if c != nil {
			if !o.imageSet && c.Config.Image != "" {
				o.image = c.Config.Image
			}
			if o.home == "" && !o.root {
				for _, m := range c.Mounts {
					if m.Destination == "/home/pdfeditor/host_fs" {
						o.root = true
					}
					if m.Destination == "/documents" {
						o.home = m.Source
					}
				}
				if o.root {
					o.home = ""
				}
			}
			if !o.portSet {
				if p := c.NetworkSettings.Ports["8080/tcp"]; len(p) > 0 {
					o.port, _ = strconv.Atoi(p[0].HostPort)
				}
			}
		}
		if _, err = a.call(ctx, true, "pull", "--platform", "linux/amd64", o.image); err != nil {
			return err
		}
		// Validate replacement prerequisites before stopping the old container.
		if _, err = a.createArgs(ctx, o, info.SecurityOptions); err != nil {
			return err
		}
		if c != nil {
			if _, err = a.call(ctx, true, "stop", o.name); err != nil {
				return err
			}
			if _, err = a.call(ctx, true, "rm", o.name); err != nil {
				return err
			}
			c = nil
		}
	}
	if c == nil {
		if info.Architecture != "x86_64" && info.Architecture != "amd64" && !o.emulation {
			return errors.New("only the linux/amd64 image is available; ARM emulation is unvalidated (opt in with --allow-emulation)")
		}
		args, e := a.createArgs(ctx, o, info.SecurityOptions)
		if e != nil {
			return e
		}
		if o.action != "update" {
			if _, e = a.call(ctx, false, "image", "inspect", o.image); e != nil {
				if _, e = a.call(ctx, true, "pull", "--platform", "linux/amd64", o.image); e != nil {
					return e
				}
			}
		}
		if _, err = a.call(ctx, true, args...); err != nil {
			return err
		}
	} else if c.State.Status != "running" {
		if _, err = a.call(ctx, true, "start", o.name); err != nil {
			return err
		}
	}
	if a.goos != "linux" {
		fmt.Fprintln(a.out, "Docker Desktop support is experimental; sandbox startup must succeed.")
	}
	c, err = a.wait(ctx, o.name)
	if err != nil {
		return err
	}
	return a.printURL(c, o.open)
}

func (a *app) createArgs(ctx context.Context, o options, security []string) ([]string, error) {
	endpoint, err := a.call(ctx, false, "context", "inspect", "--format", "{{.Endpoints.docker.Host}}")
	if err != nil {
		return nil, err
	}
	if !strings.HasPrefix(endpoint, "unix://") && !strings.HasPrefix(endpoint, "npipe://") {
		return nil, errors.New("host file sharing requires a local Docker context; remote/TCP daemons are not supported")
	}
	if os.Getenv("DOCKER_HOST") != "" && !strings.HasPrefix(os.Getenv("DOCKER_HOST"), "unix://") && !strings.HasPrefix(os.Getenv("DOCKER_HOST"), "npipe://") {
		return nil, errors.New("DOCKER_HOST points to a remote/TCP daemon; use a local context")
	}
	if o.root && a.goos != "linux" {
		return nil, errors.New("--host-root is Linux-only; Docker Desktop shares your selected host folder instead")
	}
	home := o.home
	if home == "" {
		home, err = os.UserHomeDir()
		if err != nil {
			return nil, err
		}
	}
	home, err = filepath.Abs(home)
	if err != nil {
		return nil, err
	}
	stat, err := os.Stat(home)
	if err != nil || !stat.IsDir() {
		return nil, fmt.Errorf("shared folder does not exist: %s", home)
	}
	if strings.Contains(home, ",") {
		return nil, errors.New("shared folder paths containing commas are not supported by this launcher")
	}
	profile, err := a.profile()
	if err != nil {
		return nil, err
	}
	uid, gid := a.uid, a.gid
	if a.goos == "windows" {
		uid, gid = 1000, 1000
	}
	if uid <= 0 || gid < 0 {
		return nil, errors.New("run the launcher as your normal user rather than root")
	}
	args := []string{"run", "--detach", "--name", o.name, "--label", ownerLabel + "=1", "--restart", "unless-stopped", "--platform", "linux/amd64", "--user", fmt.Sprintf("%d:%d", uid, gid), "--cap-drop", "ALL", "--security-opt", "no-new-privileges:true", "--security-opt", "seccomp=" + profile, "--read-only", "--tmpfs", "/tmp:rw,nosuid,nodev,size=256m,mode=1777", "--tmpfs", "/home/pdfeditor:rw,nosuid,nodev,size=64m,mode=1777", "--pids-limit", "256", "--shm-size", "128m", "--publish", fmt.Sprintf("127.0.0.1:%s:8080", portString(o.port)), "--mount", "type=bind,source=" + home + ",target=/documents"}
	for _, s := range security {
		if strings.Contains(s, "selinux") {
			args = append(args, "--security-opt", "label=disable")
			break
		}
	}
	if o.root {
		args = append(args, "--mount", "type=bind,source=/,target=/home/pdfeditor/host_fs,bind-propagation=rslave")
	} else {
		args = append(args, "--env", "PDF_EDITOR_INITIAL_FOLDER=/documents")
	}
	return append(args, o.image), nil
}
func portString(port int) string {
	if port == 0 {
		return ""
	}
	return strconv.Itoa(port)
}
func (a *app) profile() (string, error) {
	b, err := assets.Files.ReadFile("packaging/docker/seccomp.json")
	if err != nil {
		return "", err
	}
	hash := sha256.Sum256(b)
	directory := filepath.Join(a.cache, fmt.Sprintf("%x", hash[:8]))
	if err = os.MkdirAll(directory, 0700); err != nil {
		return "", err
	}
	for source, target := range map[string]string{"packaging/docker/seccomp.json": "seccomp.json", "LICENSE": "LICENSE", "packaging/docker/MOBY-PROFILES-LICENSE": "MOBY-PROFILES-LICENSE"} {
		content, e := assets.Files.ReadFile(source)
		if e != nil {
			return "", e
		}
		temp, e := os.CreateTemp(directory, "asset-")
		if e != nil {
			return "", e
		}
		name := temp.Name()
		_, e = temp.Write(content)
		closeErr := temp.Close()
		if e == nil {
			e = closeErr
		}
		if e == nil {
			e = os.Rename(name, filepath.Join(directory, target))
		}
		if e != nil {
			os.Remove(name)
			return "", e
		}
	}
	return filepath.Join(directory, "seccomp.json"), nil
}
func (a *app) wait(ctx context.Context, name string) (*container, error) {
	deadline := time.Now().Add(60 * time.Second)
	for time.Now().Before(deadline) {
		c, err := a.inspect(ctx, name)
		if err != nil {
			return nil, err
		}
		if c == nil {
			return nil, errors.New("container disappeared during startup")
		}
		if c.State.Status == "running" && c.State.Health != nil && c.State.Health.Status == "healthy" {
			return c, nil
		}
		if c.State.Status == "exited" || c.State.Status == "dead" || c.State.Status == "restarting" || (c.State.Health != nil && c.State.Health.Status == "unhealthy") {
			break
		}
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-time.After(time.Second):
		}
	}
	a.call(ctx, true, "logs", "--tail", "50", name)
	return nil, errors.New("startup failed: check file-sharing permissions, Bubblewrap namespaces and the logs above; no unsandboxed fallback is available")
}
func (a *app) printURL(c *container, open bool) error {
	ports := c.NetworkSettings.Ports["8080/tcp"]
	if len(ports) == 0 {
		return errors.New("container has no published browser port")
	}
	if ports[0].HostIP != "127.0.0.1" && ports[0].HostIP != "::1" {
		return errors.New("browser port is not localhost-only; inspect this container's configuration")
	}
	port, err := strconv.Atoi(ports[0].HostPort)
	if err != nil || port < 1 || port > 65535 {
		return errors.New("invalid published browser port")
	}
	url := fmt.Sprintf("http://localhost:%d/vnc.html?autoconnect=true&resize=scale", port)
	fmt.Fprintln(a.out, "Open in your browser:\n  "+url)
	if open {
		var cmd *exec.Cmd
		switch a.goos {
		case "windows":
			cmd = exec.Command("rundll32", "url.dll,FileProtocolHandler", url)
		case "darwin":
			cmd = exec.Command("open", url)
		default:
			cmd = exec.Command("xdg-open", url)
		}
		if err = cmd.Run(); err != nil {
			fmt.Fprintln(a.errOut, "Could not open the browser automatically; use the printed link:", err)
		}
	}
	return nil
}
