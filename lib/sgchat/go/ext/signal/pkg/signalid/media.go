// mautrix-signal - A Matrix-Signal puppeting bridge.
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

package signalid

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"fmt"
	"io"

	"github.com/google/uuid"
	"maunium.net/go/mautrix/bridgev2/networkid"
)

type directMediaType byte

const (
	directMediaTypeAttachment                directMediaType = 0
	directMediaTypeGroupAvatar               directMediaType = 1
	directMediaTypeProfileAvatar             directMediaType = 2
	directMediaTypePlaintextDigestAttachment directMediaType = 3
)

type DirectMediaInfo interface {
	AsMediaID() (networkid.MediaID, error)
}

var (
	_ DirectMediaInfo = (*DirectMediaAttachment)(nil)
	_ DirectMediaInfo = (*DirectMediaGroupAvatar)(nil)
	_ DirectMediaInfo = (*DirectMediaProfileAvatar)(nil)
)

type DirectMediaAttachment struct {
	CDNID           uint64
	CDNKey          string
	CDNNumber       uint32
	Key             []byte
	PlaintextDigest bool
	Digest          []byte
	Size            uint32
}

func (m DirectMediaAttachment) AsMediaID() (mediaID networkid.MediaID, err error) {
	buf := &bytes.Buffer{}

	attType := directMediaTypeAttachment
	if m.PlaintextDigest {
		attType = directMediaTypePlaintextDigestAttachment
	}

	if err = binary.Write(buf, binary.BigEndian, attType); err != nil {
		return
	} else if err = writeUvarint(buf, m.CDNID); err != nil {
		return
	} else if err = writeByteSlice(buf, []byte(m.CDNKey)); err != nil {
		return
	} else if err = writeUvarint(buf, uint64(m.CDNNumber)); err != nil {
		return
	} else if err = writeByteSlice(buf, m.Key); err != nil {
		return
	} else if err = writeByteSlice(buf, m.Digest); err != nil {
		return
	} else if err = writeUvarint(buf, uint64(m.Size)); err != nil {
		return
	}

	return networkid.MediaID(buf.Bytes()), nil
}

type DirectMediaGroupAvatar struct {
	UserID          uuid.UUID
	GroupID         [32]byte
	GroupAvatarPath string
}

func (m DirectMediaGroupAvatar) AsMediaID() (mediaID networkid.MediaID, err error) {
	buf := &bytes.Buffer{}

	if err = binary.Write(buf, binary.BigEndian, directMediaTypeGroupAvatar); err != nil {
		return
	} else if err = binary.Write(buf, binary.BigEndian, m.UserID); err != nil {
		return
	} else if err = binary.Write(buf, binary.BigEndian, m.GroupID); err != nil {
		return
	} else if err = writeByteSlice(buf, []byte(m.GroupAvatarPath)); err != nil {
		return
	}

	return networkid.MediaID(buf.Bytes()), nil
}

type DirectMediaProfileAvatar struct {
	UserID            uuid.UUID
	ContactID         uuid.UUID
	ProfileAvatarPath string
}

func (m DirectMediaProfileAvatar) AsMediaID() (mediaID networkid.MediaID, err error) {
	buf := &bytes.Buffer{}

	if err = binary.Write(buf, binary.BigEndian, directMediaTypeProfileAvatar); err != nil {
		return
	} else if err = binary.Write(buf, binary.BigEndian, m.UserID); err != nil {
		return
	} else if err = binary.Write(buf, binary.BigEndian, m.ContactID); err != nil {
		return
	} else if err = writeByteSlice(buf, []byte(m.ProfileAvatarPath)); err != nil {
		return
	}

	return networkid.MediaID(buf.Bytes()), nil
}

func ParseDirectMediaInfo(mediaID networkid.MediaID) (_ DirectMediaInfo, err error) {
	mediaIDLen := len(mediaID)
	if mediaIDLen == 0 {
		return nil, fmt.Errorf("empty media ID")
	}

	buf := bytes.NewReader(mediaID)

	// type byte
	var mediaType directMediaType
	if err := binary.Read(buf, binary.BigEndian, &mediaType); err != nil {
		return nil, fmt.Errorf("failed to read media type: %w", err)
	}

	switch mediaType {
	case directMediaTypeAttachment, directMediaTypePlaintextDigestAttachment:
		var info DirectMediaAttachment
		info.PlaintextDigest = mediaType == directMediaTypePlaintextDigestAttachment

		if info.CDNID, err = binary.ReadUvarint(buf); err != nil {
			return info, fmt.Errorf("failed to read cdn id: %w", err)
		}
		if cdnKey, err := readByteSlice(buf, mediaIDLen); err != nil {
			return info, fmt.Errorf("failed to read cdn key: %w", err)
		} else {
			info.CDNKey = string(cdnKey)
		}
		if cdnNumber, err := binary.ReadUvarint(buf); err != nil {
			return info, fmt.Errorf("failed to read cdn number: %w", err)
		} else {
			info.CDNNumber = uint32(cdnNumber)
		}
		if info.Key, err = readByteSlice(buf, mediaIDLen); err != nil {
			return info, fmt.Errorf("failed to read key: %w", err)
		} else if info.Digest, err = readByteSlice(buf, mediaIDLen); err != nil {
			return info, fmt.Errorf("failed to read digest: %w", err)
		}
		if size, err := binary.ReadUvarint(buf); err != nil {
			return info, fmt.Errorf("failed to read cdn id: %w", err)
		} else {
			info.Size = uint32(size)
		}

		return &info, nil
	case directMediaTypeGroupAvatar:
		var info DirectMediaGroupAvatar

		if err = binary.Read(buf, binary.BigEndian, &info.UserID); err != nil {
			return info, fmt.Errorf("failed to read user id: %w", err)
		} else if err = binary.Read(buf, binary.BigEndian, &info.GroupID); err != nil {
			return info, fmt.Errorf("failed to read group id: %w", err)
		}
		if groupAvatarPath, err := readByteSlice(buf, mediaIDLen); err != nil {
			return info, fmt.Errorf("failed to read group avatar path: %w", err)
		} else {
			info.GroupAvatarPath = string(groupAvatarPath)
		}

		return &info, nil
	case directMediaTypeProfileAvatar:
		var info DirectMediaProfileAvatar

		if err = binary.Read(buf, binary.BigEndian, &info.UserID); err != nil {
			return info, fmt.Errorf("failed to read user id: %w", err)
		} else if err = binary.Read(buf, binary.BigEndian, &info.ContactID); err != nil {
			return info, fmt.Errorf("failed to read contact id: %w", err)
		}
		if profileAvatarPath, err := readByteSlice(buf, mediaIDLen); err != nil {
			return info, fmt.Errorf("failed to read profile avatar path: %w", err)
		} else {
			info.ProfileAvatarPath = string(profileAvatarPath)
		}
		return &info, nil
	}

	return nil, fmt.Errorf("invalid direct media type %d", mediaType)
}

func HashMediaID(mediaID networkid.MediaID) [32]byte {
	return sha256.Sum256(mediaID)
}

func writeUvarint(w io.Writer, i uint64) error {
	_, err := w.Write(binary.AppendUvarint(nil, i))
	return err
}

func writeByteSlice(w io.Writer, b []byte) error {
	if err := writeUvarint(w, uint64(len(b))); err != nil {
		return err
	}
	_, err := w.Write(b)
	return err
}

type byteReader interface {
	io.ByteReader
	io.Reader
}

func readByteSlice(r byteReader, maxLength int) ([]byte, error) {
	length, err := binary.ReadUvarint(r)
	if err != nil {
		return nil, fmt.Errorf("reading uvarint failed: %w", err)
	} else if int(length) > maxLength {
		return nil, fmt.Errorf("byte slice size larger than expected: %d > %d", length, maxLength)
	} else if length == 0 {
		return nil, nil
	}

	buf := make([]byte, length)
	_, err = io.ReadFull(r, buf)
	return buf, err
}
