package libsignalgo

// noCopy may be added to structs which must not be copied after the first use.
// In this package, it is used for anything that uses runtime finalizers, since
// the finalizer is called when the pointer is garbage collected, even if the
// value is still referenced.
//
// See https://golang.org/issues/8005#issuecomment-190753527 for details.
//
// Note that it must not be embedded, due to the Lock and Unlock methods.
type noCopy struct{}

// Lock is a no-op used by -copylocks checker from `go vet`.
func (*noCopy) Lock()   {}
func (*noCopy) Unlock() {}
