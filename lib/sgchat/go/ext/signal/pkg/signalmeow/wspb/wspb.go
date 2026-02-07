// Package wspb provides helpers for reading and writing protobuf messages.
// Adapted from: https://github.com/nhooyr/websocket/blob/master/wspb/wspb.go
package wspb

import (
	"bytes"
	"context"
	"fmt"
	"sync"

	"github.com/coder/websocket"
	"google.golang.org/protobuf/proto"
)

// Read reads a protobuf message from c into v.
// It will reuse buffers in between calls to avoid allocations.
func Read(ctx context.Context, c *websocket.Conn, v proto.Message) error {
	return read(ctx, c, v)
}

func read(ctx context.Context, c *websocket.Conn, v proto.Message) (err error) {
	defer errd_wrap(&err, "failed to read protobuf message")

	typ, r, err := c.Reader(ctx)
	if err != nil {
		return err
	}

	if typ != websocket.MessageBinary {
		c.Close(websocket.StatusUnsupportedData, "expected binary message")
		return fmt.Errorf("expected binary message for protobuf but got: %v", typ)
	}

	b := pool_get()
	defer pool_put(b)

	_, err = b.ReadFrom(r)
	if err != nil {
		return err
	}

	err = proto.Unmarshal(b.Bytes(), v)
	if err != nil {
		c.Close(websocket.StatusInvalidFramePayloadData, "failed to unmarshal protobuf")
		return fmt.Errorf("failed to unmarshal protobuf: %w", err)
	}

	return nil
}

// Write writes the protobuf message v to c.
// It will reuse buffers in between calls to avoid allocations.
func Write(ctx context.Context, c *websocket.Conn, v proto.Message) error {
	return write(ctx, c, v)
}

func write(ctx context.Context, c *websocket.Conn, v proto.Message) (err error) {
	defer errd_wrap(&err, "failed to write protobuf message")

	data, err := proto.Marshal(v)
	if err != nil {
		return fmt.Errorf("failed to marshal protobuf: %w", err)
	}

	return c.Write(ctx, websocket.MessageBinary, data)
}

// Adapted from: bpool.go
var my_bpool sync.Pool

// Get returns a buffer from the pool or creates a new one if
// the pool is empty.
func pool_get() *bytes.Buffer {
	b := my_bpool.Get()
	if b == nil {
		return &bytes.Buffer{}
	}
	return b.(*bytes.Buffer)
}

// Put returns a buffer into the pool.
func pool_put(b *bytes.Buffer) {
	b.Reset()
	my_bpool.Put(b)
}

// Adapted from: errd.go

// Wrap wraps err with fmt.Errorf if err is non nil.
// Intended for use with defer and a named error return.
// Inspired by https://github.com/golang/go/issues/32676.
func errd_wrap(err *error, f string, v ...interface{}) {
	if *err != nil {
		*err = fmt.Errorf(f+": %w", append(v, *err)...)
	}
}
