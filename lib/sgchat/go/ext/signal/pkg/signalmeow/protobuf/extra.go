package signalpb

type BodyRangeAssociatedValue = isBodyRange_AssociatedValue

type ChatEventContent interface {
	isChatEventContent()
}

func (*DataMessage) isChatEventContent()   {}
func (*TypingMessage) isChatEventContent() {}
func (*EditMessage) isChatEventContent()   {}
