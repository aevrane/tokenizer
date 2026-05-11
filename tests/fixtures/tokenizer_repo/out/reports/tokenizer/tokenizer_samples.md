# Shared Tokenizer Samples

## neutral prose

- input: `An estuary is a coastal body of water where river water mixes with seawater.`
- normalized: `An estuary is a coastal body of water where river water mixes with seawater.`
- decoded: `An estuary is a coastal body of water where river water mixes with seawater.`
- token ids: `69, 114, 36, 105, 119, 120, 121, 101, 118, 125, 36, 109, 119, 36, 101, 36, 103, 115, 101, 119, 120, 101, 112, 36, 102, 115, 104, 125, 36, 115, 106, 36, 123, 101, 120, 267, 36, 123, 108, 267, 105, 36, 118, 260, 267, 36, 123, 101, 120, 267, 36, 113, 286, 105, 119, 36, 123, 109, 120, 108, 36, 119, 105, 101, 123, 101, 120, 267, 50`
- token pieces: `A | n |   | e | s | t | u | a | r | y |   | i | s |   | a |   | c | o | a | s | t | a | l |   | b | o | d | y |   | o | f |   | w | a | t | er |   | w | h | er | e |   | r | iv | er |   | w | a | t | er |   | m | ix | e | s |   | w | i | t | h |   | s | e | a | w | a | t | er | .`

## conversation

- input: `The record does not support that claim, so I cannot state it as fact.`
- normalized: `The record does not support that claim, so I cannot state it as fact.`
- decoded: `The record does not support that claim, so I cannot state it as fact.`
- token ids: `88, 108, 105, 36, 118, 105, 103, 115, 118, 104, 36, 104, 115, 105, 119, 36, 114, 115, 120, 36, 119, 121, 116, 116, 115, 118, 120, 36, 120, 108, 101, 120, 36, 103, 112, 101, 109, 113, 48, 36, 119, 115, 36, 77, 36, 103, 101, 114, 114, 115, 120, 36, 119, 120, 101, 120, 105, 36, 109, 120, 36, 101, 119, 36, 106, 101, 103, 120, 50`
- token pieces: `T | h | e |   | r | e | c | o | r | d |   | d | o | e | s |   | n | o | t |   | s | u | p | p | o | r | t |   | t | h | a | t |   | c | l | a | i | m | , |   | s | o |   | I |   | c | a | n | n | o | t |   | s | t | a | t | e |   | i | t |   | a | s |   | f | a | c | t | .`

## source attribution

- input: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- normalized: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- decoded: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- token ids: `87, 115, 121, 118, 103, 105, 36, 116, 121, 102, 112, 109, 103, 62, 51, 51, 116, 113, 103, 51, 84, 81, 71, 56, 56, 57, 59, 52, 57, 61, 36, 44, 54, 52, 53, 57, 49, 58, 49, 56, 45, 36, 118, 105, 116, 115, 118, 120, 119, 36, 120, 108, 101, 120, 36, 120, 108, 105, 36, 103, 112, 101, 109, 113, 36, 109, 119, 36, 120, 109, 105, 104, 36, 120, 115, 36, 120, 108, 105, 36, 104, 105, 119, 103, 118, 109, 102, 105, 104, 36, 119, 120, 121, 104, 125, 50`
- token pieces: `S | o | u | r | c | e |   | p | u | b | l | i | c | : | / | / | p | m | c | / | P | M | C | 4 | 4 | 5 | 7 | 0 | 5 | 9 |   | ( | 2 | 0 | 1 | 5 | - | 6 | - | 4 | ) |   | r | e | p | o | r | t | s |   | t | h | a | t |   | t | h | e |   | c | l | a | i | m |   | i | s |   | t | i | e | d |   | t | o |   | t | h | e |   | d | e | s | c | r | i | b | e | d |   | s | t | u | d | y | .`

## uncertainty and abstention

- input: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- normalized: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- decoded: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- token ids: `88, 108, 105, 36, 101, 122, 101, 109, 112, 101, 102, 112, 105, 36, 109, 114, 116, 121, 120, 119, 36, 104, 115, 36, 114, 115, 120, 36, 116, 118, 115, 265, 104, 105, 36, 105, 114, 115, 121, 107, 108, 36, 283, 109, 104, 105, 114, 103, 105, 48, 36, 119, 115, 36, 120, 108, 105, 36, 119, 108, 105, 112, 112, 36, 119, 108, 115, 121, 112, 104, 36, 116, 118, 105, 119, 267, 122, 105, 36, 121, 114, 103, 267, 120, 101, 109, 114, 120, 125, 36, 115, 118, 36, 101, 102, 119, 120, 101, 109, 114, 50`
- token pieces: `T | h | e |   | a | v | a | i | l | a | b | l | e |   | i | n | p | u | t | s |   | d | o |   | n | o | t |   | p | r | o | vi | d | e |   | e | n | o | u | g | h |   | ev | i | d | e | n | c | e | , |   | s | o |   | t | h | e |   | s | h | e | l | l |   | s | h | o | u | l | d |   | p | r | e | s | er | v | e |   | u | n | c | er | t | a | i | n | t | y |   | o | r |   | a | b | s | t | a | i | n | .`

## project verbalization

- input: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- normalized: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- decoded: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- token ids: `88, 108, 105, 36, 103, 115, 114, 106, 109, 104, 105, 114, 103, 105, 36, 115, 102, 110, 105, 103, 120, 36, 109, 114, 104, 109, 103, 101, 120, 105, 119, 36, 112, 115, 123, 36, 103, 115, 114, 106, 109, 104, 105, 114, 103, 105, 48, 36, 119, 115, 36, 120, 108, 105, 36, 119, 108, 105, 112, 112, 36, 119, 108, 115, 121, 112, 104, 36, 116, 118, 105, 119, 267, 122, 105, 36, 121, 114, 103, 267, 120, 101, 109, 114, 120, 125, 36, 118, 101, 120, 108, 267, 36, 120, 108, 101, 114, 36, 115, 122, 267, 119, 120, 101, 120, 105, 36, 120, 108, 105, 36, 103, 112, 101, 109, 113, 50`
- token pieces: `T | h | e |   | c | o | n | f | i | d | e | n | c | e |   | o | b | j | e | c | t |   | i | n | d | i | c | a | t | e | s |   | l | o | w |   | c | o | n | f | i | d | e | n | c | e | , |   | s | o |   | t | h | e |   | s | h | e | l | l |   | s | h | o | u | l | d |   | p | r | e | s | er | v | e |   | u | n | c | er | t | a | i | n | t | y |   | r | a | t | h | er |   | t | h | a | n |   | o | v | er | s | t | a | t | e |   | t | h | e |   | c | l | a | i | m | .`

