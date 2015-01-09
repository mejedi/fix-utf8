#include <iostream>
#include <vector>
#include <string>

inline unsigned char utf8b_1(unsigned char c) { return 0xed; }
inline unsigned char utf8b_2(unsigned char c) { return 0xac + (c >> 6); }
inline unsigned char utf8b_3(unsigned char c) { return 0x80 | (c & 0x3f); }

void
escape_character(std::string &str, char ch)
{
    str += utf8b_1(ch);
    str += utf8b_2(ch);
    str += utf8b_3(ch);
}

void
escape_sequence(std::string &str, const std::string &s, size_t begin, size_t end)
{
	for (size_t i = begin; i <= end; i++)
		escape_character(str, s[i]);
}

int
utf8len(unsigned char first)
{
	if ((first & 0x80) == 0)
		return 1;
	first >>= 3;
	if (first == 0x1E)
		return 4;
	first >>= 1;
	if (first == 0x0E)
		return 3;
	first >>= 1;
	if (first == 0x06)
		return 2;
	return -1;
}

void
escape_control(std::string &str, char ch)
{
	switch (ch) {
	case '"':
		str += "\\\"";
		break;
	case '\\':
		str += "\\\\";
		break;
	case '/':
		str += "\\/";
		break;
	case '\b':
		str += "\\b";
		break;
	case '\f':
		str += "\\f";
		break;
	case '\n':
		str += "\\n";
		break;
	case '\r':
		str += "\\r";
		break;
	case '\t':
		str += "\\t";
		break;
	default:
		if ((0 <= ch && ch <= 0x1f) || ch == 0x7f)
			escape_character(str, ch);
		else
			str += ch;
	}
}

#define L2OVERLONG(c1) (((c1) & 0x1E) == 0)

#define L3OVERLONG(c1, c2) ((((c1) & 0x0F) == 0) && (((c2) & 0x20) == 0))
#define L3SURROGATE(c1, c2) ((((unsigned char)(c1)) == 0xED) && \
		((unsigned char)(c2)) >= 0xA0 && ((unsigned char)(c2)) <= 0xBF)

#define L4OVERLONG(c1, c2) ((((c1) & 0x07) == 0) && (((c2) & 0x30) == 0))
#define L4OVERMAX(c1, c2) (((c1) & 0x04) && (((c1) & 0x03) || ((c2) & 0x30)))

/* Check, if this byte can be one of the 2,3,4 bytes of utf8 sequence
 */
bool
contb(unsigned char c)
{
	return (c & 0xC0) == 0x80;
}

bool
validate_seq(std::string &ns, const std::string &s, size_t &i, size_t len)
{
	for (size_t j = 1; j < len; j++) {
		if (!contb(s[i + j])) {
			for (size_t k = 0; k < j; k++) {
				escape_character(ns, s[i + k]);
			}
			i += j;
			return false;
		}
	}
	return true;
}

/* We need to escape not utf8 sequences and
 * control sequences. What we do:
 * get first symbol of string. If it can be the first symbol 
 * of utf8 sequence, we find the length of sequence from it.
 * Otherwise, we write it as \u00xx. If the sequence length is 1,
 * we write it to the output string, checking, if it is the control
 * symbol (\n, \t, etc). If the length is > 1,
 * we read length - 1 next symbols. Each next symbol must have form
 * 10xxxxxx. If it doesn't, the sequence is incorrect and we write it
 * to the output string, using escaping \u00xx. If we reached end of input string
 * before ending sequence, we do the same. If we have read all the
 * sequence, and it is correct, we put it to the output string as is.
 * 
 * After, make the same with the next symbols. */
std::string
to_json_string(const std::string &s)
{
	std::string ns;
	size_t i, s_len;
	int charlen;

	ns.reserve(s.size());
	//ns += "\"";
	s_len = s.length();
	i = 0;
	while (i < s_len) {
		char ch = s[i];
		charlen = utf8len((unsigned char)ch);
		if (charlen > 0 && s_len  < i + charlen) {
			while (i < s_len)
				escape_character(ns, s[i++]);
			continue;
		}
		switch (charlen) {
		case 1:
			/* 1-byte sequence. 
			 * we need to escape control sequences */
			// escape_control(ns, ch);
            ns += ch;
			i++;
			break;
		case 2:
			if (validate_seq(ns, s, i, 2)) {
				/* check overlong */
				if (L2OVERLONG(s[i])) {
					escape_character(ns, s[i]);
					escape_character(ns, s[i + 1]);
				} else {
					ns += s[i];
					ns += s[i + 1];
				}
				i += 2;
			}
			break;
		case 3:
			if (validate_seq(ns, s, i, 3)) {
				/* check overlong */
				if (L3OVERLONG(s[i], s[i + 1]) ||
				    L3SURROGATE(s[i], s[i + 1])) {
					escape_character(ns, s[i]);
					escape_character(ns, s[i + 1]);
					escape_character(ns, s[i + 2]);
				} else {
					ns += s[i];
					ns += s[i + 1];
					ns += s[i + 2];
				}
				i += 3;
			}
			break;
		case 4:
			if (validate_seq(ns, s, i, 4)) {
				/* check overlong and if symbol <= 0x10FFFF */
				if (L4OVERLONG(s[i], s[i + 1]) ||
				    L4OVERMAX(s[i], s[i + 1])) {
					escape_character(ns, s[i]);
					escape_character(ns, s[i + 1]);
					escape_character(ns, s[i + 2]);
					escape_character(ns, s[i + 3]);
				} else {
					ns += s[i];
					ns += s[i + 1];
					ns += s[i + 2];
					ns += s[i + 3];
				}
				i += 4;
			}
			break;
		case -1:
		default:
			/* incorrect first symbol */
			escape_character(ns, ch);
			i++;
			break;
		}
	}
	//ns += "\"";
	return ns;
}
