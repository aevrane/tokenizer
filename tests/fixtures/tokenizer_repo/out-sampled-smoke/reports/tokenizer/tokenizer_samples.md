# Shared Tokenizer Samples

## neutral prose

- input: `An estuary is a coastal body of water where river water mixes with seawater.`
- normalized: `An estuary is a coastal body of water where river water mixes with seawater.`
- decoded: `An estuary is a coastal body of water where river water mixes with seawater.`
- token ids: `425, 345, 315, 405, 314, 448, 461, 483, 469, 471, 482, 309, 476, 280, 338, 280, 436, 461, 341, 280, 338, 330, 437, 378, 455, 338, 479`
- token pieces: `▁An | ▁estuary | ▁is | ▁a | ▁co | astal | ▁ | b | o | d | y | ▁o | f | ▁w | ater | ▁w | here | ▁ | river | ▁w | ater | ▁m | ixes | ▁with | ▁seaw | ater | .`

## conversation

- input: `The record does not support that claim, so I cannot state it as fact.`
- normalized: `The record does not support that claim, so I cannot state it as fact.`
- decoded: `The record does not support that claim, so I cannot state it as fact.`
- token ids: `371, 283, 470, 275, 471, 358, 469, 271, 443, 446, 334, 463, 457, 321, 488, 428, 461, 503, 441, 466, 401, 262, 463, 332, 461, 286, 461, 350, 461, 476, 467, 327, 479`
- token pieces: `▁The | ▁re | c | or | d | ▁d | o | es | ▁not | ▁sup | por | t | ▁that | ▁claim | , | ▁so | ▁ | I | ▁can | n | ot | ▁s | t | ate | ▁ | it | ▁ | as | ▁ | f | a | ct | .`

## source attribution

- input: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- normalized: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- decoded: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- token ids: `404, 319, 277, 451, 410, 475, 478, 470, 486, 84, 81, 506, 499, 499, 500, 59, 487, 500, 61, 424, 487, 53, 500, 484, 501, 484, 391, 283, 450, 457, 278, 321, 315, 268, 465, 284, 268, 469, 278, 372, 361, 483, 284, 262, 287, 471, 482, 479`
- token pieces: `▁S | ource | ▁p | ublic | :// | p | m | c | / | <0x50> | <0x4D> | C | 4 | 4 | 5 | <0x37> | 0 | 5 | <0x39> | ▁(2 | 0 | <0x31> | 5 | - | 6 | - | 4) | ▁re | ports | ▁that | ▁the | ▁claim | ▁is | ▁t | i | ed | ▁t | o | ▁the | ▁des | cri | b | ed | ▁s | tu | d | y | .`

## uncertainty and abstention

- input: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- normalized: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- decoded: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- token ids: `371, 405, 477, 467, 465, 290, 395, 462, 288, 475, 474, 329, 358, 469, 443, 444, 477, 465, 471, 462, 461, 274, 270, 480, 472, 461, 462, 477, 465, 299, 488, 428, 278, 322, 302, 385, 347, 461, 275, 405, 483, 292, 281, 479`
- token pieces: `▁The | ▁a | v | a | i | la | bl | e | ▁in | p | u | ts | ▁d | o | ▁not | ▁pro | v | i | d | e | ▁ | en | ou | g | h | ▁ | e | v | i | dence | , | ▁so | ▁the | ▁shell | ▁should | ▁preserve | ▁uncertainty | ▁ | or | ▁a | b | st | ain | .`

## project verbalization

- input: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- normalized: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- decoded: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- token ids: `371, 346, 379, 288, 416, 432, 442, 346, 488, 428, 278, 322, 302, 385, 347, 445, 333, 456, 454, 342, 278, 321, 479`
- token pieces: `▁The | ▁confidence | ▁object | ▁in | dic | ates | ▁low | ▁confidence | , | ▁so | ▁the | ▁shell | ▁should | ▁preserve | ▁uncertainty | ▁rat | her | ▁than | ▁over | state | ▁the | ▁claim | .`

