// mautrix-signal - A Matrix-Signal puppeting bridge.
// Copyright (C) 2023 Tulir Asokan
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

package signalfmt

import (
	"fmt"
	"sort"

	"google.golang.org/protobuf/proto"

	signalpb "go.mau.fi/mautrix-signal/pkg/signalmeow/protobuf"
)

type BodyRange struct {
	Start  int
	Length int
	Value  BodyRangeValue
}

type BodyRangeList []BodyRange

var _ sort.Interface = BodyRangeList(nil)

func (b BodyRangeList) Len() int {
	return len(b)
}

func (b BodyRangeList) Less(i, j int) bool {
	return b[i].Start < b[j].Start || b[i].Length > b[j].Length
}

func (b BodyRangeList) Swap(i, j int) {
	b[i], b[j] = b[j], b[i]
}

func (b BodyRange) String() string {
	return fmt.Sprintf("%d:%d:%v", b.Start, b.Length, b.Value)
}

// End returns the end index of the range.
func (b BodyRange) End() int {
	return b.Start + b.Length
}

// Offset changes the start of the range without affecting the length.
func (b BodyRange) Offset(offset int) *BodyRange {
	b.Start += offset
	return &b
}

// TruncateStart changes the length of the range, so it starts at the given index and ends at the same index as before.
func (b BodyRange) TruncateStart(startAt int) *BodyRange {
	if b.Start < startAt {
		b.Length -= startAt - b.Start
		b.Start = startAt
	}
	return &b
}

// TruncateEnd changes the length of the range, so it ends at or before the given index and starts at the same index as before.
func (b BodyRange) TruncateEnd(maxEnd int) *BodyRange {
	if b.End() > maxEnd {
		b.Length = maxEnd - b.Start
	}
	return &b
}

func (b BodyRange) Proto() *signalpb.BodyRange {
	return &signalpb.BodyRange{
		Start:           proto.Uint32(uint32(b.Start)),
		Length:          proto.Uint32(uint32(b.Length)),
		AssociatedValue: b.Value.Proto(),
	}
}

// LinkedRangeTree is a linked tree of formatting entities.
//
// It's meant to parse a list of Signal body ranges into nodes that either overlap completely or not at all,
// which enables more natural conversion to HTML.
type LinkedRangeTree struct {
	Node    *BodyRange
	Sibling *LinkedRangeTree
	Child   *LinkedRangeTree
}

func ptrAdd(to **LinkedRangeTree, r *BodyRange) {
	if *to == nil {
		*to = &LinkedRangeTree{}
	}
	(*to).Add(r)
}

// Add adds the given formatting entity to this tree.
func (lrt *LinkedRangeTree) Add(r *BodyRange) {
	if lrt.Node == nil {
		lrt.Node = r
		return
	}
	lrtEnd := lrt.Node.End()
	if r.Start >= lrtEnd {
		ptrAdd(&lrt.Sibling, r.Offset(-lrtEnd))
		return
	}
	if r.End() > lrtEnd {
		ptrAdd(&lrt.Sibling, r.TruncateStart(lrtEnd).Offset(-lrtEnd))
	}
	ptrAdd(&lrt.Child, r.TruncateEnd(lrtEnd).Offset(-lrt.Node.Start))
}
