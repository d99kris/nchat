// mautrix-signal - A Matrix-signal puppeting bridge.
// Copyright (C) 2025 Tulir Asokan
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

package signalmeow

import (
	"bufio"
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"errors"
	"fmt"
	"hash"
	"io"
	"os"

	"google.golang.org/protobuf/proto"
)

func verifyMACStream(hmacKey [32]byte, input io.Reader, totalSize int64) (bool, error) {
	if totalSize <= 0 {
		file, ok := input.(*os.File)
		if ok {
			stat, err := file.Stat()
			if err != nil {
				return false, fmt.Errorf("failed to stat file: %w", err)
			}
			totalSize = stat.Size()
		} else {
			return false, fmt.Errorf("total size is unknown")
		}
	}
	hasher := hmac.New(sha256.New, hmacKey[:])
	_, err := io.CopyN(hasher, input, totalSize-MACLength)
	if err != nil {
		return false, fmt.Errorf("failed to hash file: %w", err)
	}
	actualHash := hasher.Sum(nil)
	expectedHash := make([]byte, MACLength)
	_, err = io.ReadFull(input, expectedHash)
	if err != nil {
		return false, fmt.Errorf("failed to read hash: %w", err)
	}
	return hmac.Equal(expectedHash, actualHash), nil
}

type decryptingReader struct {
	input         io.Reader
	cipher        cipher.BlockMode
	hasher        hash.Hash
	aesKey        *[32]byte
	remainingSize int64
}

func (dr *decryptingReader) Read(p []byte) (int, error) {
	if dr.remainingSize == 0 {
		return 0, io.EOF
	} else if len(p) < aes.BlockSize {
		return 0, fmt.Errorf("buffer too small (must be at least %d bytes)", aes.BlockSize)
	}
	if dr.cipher == nil {
		iv := make([]byte, IVLength)
		_, err := io.ReadFull(dr.input, iv)
		if err != nil {
			return 0, fmt.Errorf("failed to read IV: %w", err)
		}
		block, err := aes.NewCipher(dr.aesKey[:])
		if err != nil {
			return 0, fmt.Errorf("failed to create cipher: %w", err)
		}
		dr.cipher = cipher.NewCBCDecrypter(block, iv)
		dr.hasher.Write(iv)
	}
	maxLen := int64(len(p) - len(p)%aes.BlockSize)
	if maxLen > dr.remainingSize {
		maxLen = dr.remainingSize
	}
	p = p[:maxLen]
	_, err := io.ReadFull(dr.input, p)
	if err != nil {
		return 0, err
	}
	dr.remainingSize -= maxLen
	dr.hasher.Write(p)
	dr.cipher.CryptBlocks(p, p)
	if dr.remainingSize == 0 {
		p, err = UnpadPKCS7(p)
		if err != nil {
			return 0, fmt.Errorf("failed to unpad: %w", err)
		}
		expectedMAC := make([]byte, MACLength)
		_, err = io.ReadFull(dr.input, expectedMAC)
		if err != nil {
			return 0, fmt.Errorf("failed to read MAC: %w", err)
		}
		actualMAC := dr.hasher.Sum(nil)
		if !hmac.Equal(expectedMAC, actualMAC) {
			return 0, fmt.Errorf("hmac mismatch")
		}
	}
	return len(p), nil
}

func (dr *decryptingReader) Close() error {
	if dr.remainingSize != 0 {
		return fmt.Errorf("unexpected remaining size %d", dr.remainingSize)
	}
	return nil
}

func aesDecryptStream(aesKey, macKey [32]byte, input io.Reader, totalSize int64) io.ReadCloser {
	return &decryptingReader{
		aesKey:        &aesKey,
		hasher:        hmac.New(sha256.New, macKey[:]),
		input:         input,
		remainingSize: totalSize - MACLength - IVLength,
	}
}

func splitChunksStream(input io.Reader, callback func([]byte) error) error {
	byteReader, ok := input.(io.ByteReader)
	if !ok {
		bufInput := bufio.NewReader(input)
		byteReader = bufInput
		input = bufInput
	}
	var cachedBuf []byte
	for {
		msgLen, err := binary.ReadUvarint(byteReader)
		if errors.Is(err, io.EOF) {
			return nil
		} else if err != nil {
			return fmt.Errorf("failed to read chunk length: %w", err)
		}
		if msgLen == 0 {
			continue
		}
		if msgLen > uint64(len(cachedBuf)) {
			cachedBuf = make([]byte, max(msgLen, 8192))
		}
		buf := cachedBuf[:msgLen]
		_, err = io.ReadFull(input, buf)
		if err != nil {
			return fmt.Errorf("failed to read chunk: %w", err)
		}
		err = callback(buf)
		if err != nil {
			return err
		}
	}
}

func splitProtoChunksStream[ChunkType proto.Message](input io.Reader, callback func(ChunkType) error) error {
	protoReflect := (*new(ChunkType)).ProtoReflect()
	return splitChunksStream(input, func(buf []byte) error {
		msg := protoReflect.New().Interface().(ChunkType)
		err := proto.Unmarshal(buf, msg)
		if err != nil {
			return fmt.Errorf("failed to unmarshal chunk: %w", err)
		}
		return callback(msg)
	})
}
