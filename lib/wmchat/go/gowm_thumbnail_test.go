package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func writeTestJPEG(t *testing.T, path string) {
	t.Helper()
	cmd := exec.Command("ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
		"-f", "lavfi", "-i", "color=c=red:s=320x240:d=0.1", "-frames:v", "1", path)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Skipf("ffmpeg unavailable: %v (%s)", err, out)
	}
}

func TestEncodeJPEGThumbnail(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "src.jpg")
	writeTestJPEG(t, src)

	thumb := EncodeJPEGThumbnail(src)
	if len(thumb) < 24 {
		t.Fatalf("thumbnail too small: %d bytes", len(thumb))
	}
	if thumb[0] != 0xff || thumb[1] != 0xd8 {
		t.Fatalf("thumbnail is not JPEG (got %x)", thumb[:2])
	}
	if len(thumb) > 20*1024 {
		t.Fatalf("thumbnail too large for WhatsApp preview: %d", len(thumb))
	}
}

func TestGenerateJPEGThumbnail(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "src.jpg")
	writeTestJPEG(t, src)

	thumb, width, height := GenerateJPEGThumbnail(src)
	if len(thumb) < 24 {
		t.Fatalf("empty thumbnail")
	}
	if width != 320 || height != 240 {
		t.Fatalf("probe size %dx%d want 320x240", width, height)
	}
}

func TestEncodeJPEGThumbnailMissingFile(t *testing.T) {
	thumb := EncodeJPEGThumbnail(filepath.Join(t.TempDir(), "missing.jpg"))
	if thumb != nil {
		t.Fatalf("expected nil for missing file")
	}
}

func TestEncodeJPEGThumbnailFromPNG(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "src.png")
	cmd := exec.Command("ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
		"-f", "lavfi", "-i", "color=c=blue:s=640x480:d=0.1", "-frames:v", "1", src)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Skipf("ffmpeg unavailable: %v (%s)", err, out)
	}
	if _, err := os.Stat(src); err != nil {
		t.Fatal(err)
	}
	thumb := EncodeJPEGThumbnail(src)
	if len(thumb) < 24 || thumb[0] != 0xff || thumb[1] != 0xd8 {
		t.Fatalf("png source did not yield JPEG thumbnail")
	}
}
