// SPDX-License-Identifier: GPL-3.0-only
package launcher

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	assets "pdf-editor"
	"strings"
	"testing"
)

type fakeDocker struct {
	c        *container
	calls    [][]string
	home     string
	arch     string
	remote   bool
	failPull bool
}

func (f *fakeDocker) run(_ context.Context, _ bool, args ...string) (string, error) {
	f.calls = append(f.calls, append([]string(nil), args...))
	switch args[0] {
	case "info":
		arch := f.arch
		if arch == "" {
			arch = "x86_64"
		}
		return `{"OSType":"linux","Architecture":"` + arch + `","SecurityOptions":["name=selinux"]}`, nil
	case "context":
		if f.remote {
			return "ssh://host", nil
		}
		return "unix:///var/run/docker.sock", nil
	case "container":
		if f.c == nil {
			return "", errors.New("No such container")
		}
		b, _ := json.Marshal([]container{*f.c})
		return string(b), nil
	case "image":
		return "present", nil
	case "pull":
		if f.failPull {
			return "", errors.New("registry unavailable")
		}
		return "", nil
	case "start":
		f.c.State.Status = "running"
		return "", nil
	case "stop":
		f.c.State.Status = "exited"
		return "", nil
	case "rm":
		f.c = nil
		return "", nil
	case "run":
		f.c = healthy()
		return "created", nil
	case "logs", "build":
		return "", nil
	}
	return "", errors.New("unexpected Docker call")
}
func healthy() *container {
	var c container
	json.Unmarshal([]byte(`{"State":{"Status":"running","Health":{"Status":"healthy"}},"Config":{"Labels":{"org.pdf-editor.launcher":"1"}},"NetworkSettings":{"Ports":{"8080/tcp":[{"HostIP":"127.0.0.1","HostPort":"19081"}]}}}`), &c)
	return &c
}
func setup(t *testing.T, f *fakeDocker) (*app, *bytes.Buffer, options) {
	t.Helper()
	out := new(bytes.Buffer)
	return &app{call: f.run, out: out, errOut: out, cache: t.TempDir(), goos: "linux", uid: 1000, gid: 1000}, out, options{action: "start", image: DefaultImage, name: "test-editor", home: t.TempDir(), port: 8080, jobs: 4}
}
func hasCall(f *fakeDocker, command string) bool {
	for _, a := range f.calls {
		if a[0] == command {
			return true
		}
	}
	return false
}
func TestStartCreatesIsolatedContainerAndPrintsActualPort(t *testing.T) {
	f := new(fakeDocker)
	a, out, o := setup(t, f)
	if err := a.execute(context.Background(), o); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "localhost:19081/") {
		t.Fatal(out.String())
	}
	var args []string
	for _, c := range f.calls {
		if c[0] == "run" {
			args = c
		}
	}
	joined := strings.Join(args, " ")
	for _, value := range []string{"--user 1000:1000", "--cap-drop ALL", "--read-only", "--restart unless-stopped", "--publish 127.0.0.1:8080:8080", "PDF_EDITOR_INITIAL_FOLDER=/documents", "label=disable", "target=/documents"} {
		if !strings.Contains(joined, value) {
			t.Errorf("missing %s", value)
		}
	}
	if strings.Contains(joined, "source=/,") {
		t.Fatal("unexpected root mount")
	}
	for i, v := range args {
		if v == "--security-opt" && strings.HasPrefix(args[i+1], "seccomp=") {
			path := strings.TrimPrefix(args[i+1], "seccomp=")
			got, e := os.ReadFile(path)
			if e != nil {
				t.Fatal(e)
			}
			want, _ := assets.Files.ReadFile("packaging/docker/seccomp.json")
			if !bytes.Equal(got, want) {
				t.Fatal("embedded policy changed")
			}
		}
	}
}
func TestExistingStartPreservesSession(t *testing.T) {
	f := &fakeDocker{c: healthy()}
	a, _, o := setup(t, f)
	if e := a.execute(context.Background(), o); e != nil {
		t.Fatal(e)
	}
	for _, cmd := range []string{"run", "pull", "start", "stop", "rm"} {
		if hasCall(f, cmd) {
			t.Fatal("unexpected", cmd)
		}
	}
}
func TestStoppedStartAndStop(t *testing.T) {
	f := &fakeDocker{c: healthy()}
	f.c.State.Status = "exited"
	a, _, o := setup(t, f)
	if e := a.execute(context.Background(), o); e != nil {
		t.Fatal(e)
	}
	if !hasCall(f, "start") {
		t.Fatal("not started")
	}
	o.action = "stop"
	if e := a.execute(context.Background(), o); e != nil {
		t.Fatal(e)
	}
	if f.c.State.Status != "exited" {
		t.Fatal("not stopped")
	}
}
func TestRemoteContextAndCommaPathRejectedBeforeCreation(t *testing.T) {
	for _, remote := range []bool{true, false} {
		f := &fakeDocker{remote: remote}
		a, _, o := setup(t, f)
		if !remote {
			o.home = filepath.Join(t.TempDir(), "with,comma")
			os.Mkdir(o.home, 0700)
		}
		if e := a.execute(context.Background(), o); e == nil {
			t.Fatal("accepted invalid sharing")
		}
		if hasCall(f, "run") || hasCall(f, "pull") {
			t.Fatal("mutation before validation")
		}
	}
}
func TestARMRequiresOptInBeforeUpdateMutation(t *testing.T) {
	f := &fakeDocker{c: healthy(), arch: "aarch64"}
	a, _, o := setup(t, f)
	o.action = "update"
	if e := a.execute(context.Background(), o); e == nil {
		t.Fatal("ARM accepted")
	}
	if hasCall(f, "stop") || hasCall(f, "pull") {
		t.Fatal("mutation before compatibility check")
	}
}
func TestUpdatePullFailureKeepsContainer(t *testing.T) {
	f := &fakeDocker{c: healthy(), failPull: true}
	a, _, o := setup(t, f)
	o.action = "update"
	if e := a.execute(context.Background(), o); e == nil {
		t.Fatal("pull failure ignored")
	}
	if hasCall(f, "stop") || hasCall(f, "rm") {
		t.Fatal("old container removed on failed pull")
	}
}
func TestForeignAndComposeUpdateRejected(t *testing.T) {
	for _, label := range []map[string]string{{}, {"com.docker.compose.service": "pdf-editor"}} {
		f := &fakeDocker{c: healthy()}
		f.c.Config.Labels = label
		a, _, o := setup(t, f)
		o.action = "update"
		if e := a.execute(context.Background(), o); e == nil {
			t.Fatal("ownership check missing")
		}
		if hasCall(f, "stop") || hasCall(f, "pull") {
			t.Fatal("unowned container changed")
		}
	}
}
func TestUnhealthyFailsClosed(t *testing.T) {
	f := &fakeDocker{c: healthy()}
	f.c.State.Health.Status = "unhealthy"
	a, _, o := setup(t, f)
	if e := a.execute(context.Background(), o); e == nil {
		t.Fatal("unhealthy accepted")
	}
	if !hasCall(f, "logs") {
		t.Fatal("no diagnostics")
	}
}
func TestNoPublishedPortAndPublicEndpointRejected(t *testing.T) {
	c := healthy()
	a, _, _ := setup(t, new(fakeDocker))
	c.NetworkSettings.Ports["8080/tcp"][0].HostIP = "0.0.0.0"
	if e := a.printURL(c, false); e == nil {
		t.Fatal("public endpoint accepted")
	}
	c.NetworkSettings.Ports = nil
	if e := a.printURL(c, false); e == nil {
		t.Fatal("missing port accepted")
	}
}
func TestInvalidFlagsAndHelp(t *testing.T) {
	for _, args := range [][]string{{"bad"}, {"start", "--port", "-1"}, {"build", "--jobs", "9"}, {"start", "--home", "x", "--host-root"}} {
		if _, e := parse(args, new(bytes.Buffer)); e == nil {
			t.Fatal(args)
		}
	}
	if e := Run(context.Background(), []string{"--help"}, new(bytes.Buffer), new(bytes.Buffer)); e != nil {
		t.Fatal(e)
	}
}

func TestUpdateRetainsSharingAndHonorsExplicitPort(t *testing.T) {
	f := &fakeDocker{c: healthy()}
	a, _, o := setup(t, f)
	originalHome := t.TempDir()
	f.c.Mounts = []struct{ Source, Destination string }{{originalHome, "/documents"}}
	f.c.Config.Image = "example/pdf:local"
	o.action = "update"
	o.home = ""
	o.port = 19082
	o.portSet = true
	if e := a.execute(context.Background(), o); e != nil {
		t.Fatal(e)
	}
	var run, pull string
	for _, args := range f.calls {
		if args[0] == "run" {
			run = strings.Join(args, " ")
		}
		if args[0] == "pull" {
			pull = strings.Join(args, " ")
		}
	}
	if !strings.Contains(run, "source="+originalHome+",target=/documents") || !strings.Contains(run, "127.0.0.1:19082:8080") || !strings.Contains(pull, "example/pdf:local") {
		t.Fatal(run, pull)
	}
}
func TestDesktopUsesSharedFolderAndRejectsHostRoot(t *testing.T) {
	f := new(fakeDocker)
	a, _, o := setup(t, f)
	a.goos = "darwin"
	a.uid = 501
	a.gid = 20
	args, e := a.createArgs(context.Background(), o, nil)
	if e != nil {
		t.Fatal(e)
	}
	joined := strings.Join(args, " ")
	if !strings.Contains(joined, "--user 501:20") || !strings.Contains(joined, "PDF_EDITOR_INITIAL_FOLDER=/documents") {
		t.Fatal(joined)
	}
	o.root = true
	if _, e = a.createArgs(context.Background(), o, nil); e == nil {
		t.Fatal("desktop root mount accepted")
	}
}
