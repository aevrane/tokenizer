# Shared Tokenizer Samples

## neutral prose

- input: `An estuary is a coastal body of water where river water mixes with seawater.`
- normalized: `An estuary is a coastal body of water where river water mixes with seawater.`
- decoded: `An estuary is a coastal body of water where river water mixes with seawater.`
- token ids: `870, 1221, 2631, 312, 261, 5431, 1072, 280, 722, 827, 2955, 722, 3376, 273, 354, 17927, 22524`
- token pieces: `[ws]An | [ws]est | uary | [ws]is | [ws]a | [ws]coastal | [ws]body | [ws]of | [ws]water | [ws]where | [ws]river | [ws]water | [ws]mix | es | [ws]with | [ws]seawater | .`

## conversation

- input: `The record does not support that claim, so I cannot state it as fact.`
- normalized: `The record does not support that claim, so I cannot state it as fact.`
- decoded: `The record does not support that claim, so I cannot state it as fact.`
- token ids: `331, 1548, 1104, 452, 1230, 436, 341, 2879, 22525, 588, 334, 2734, 1204, 358, 348, 1004, 22524`
- token pieces: `[ws]The | [ws]record | [ws]does | [ws]not | [ws]supp | ort | [ws]that | [ws]claim | , | [ws]so | [ws]I | [ws]cannot | [ws]state | [ws]it | [ws]as | [ws]fact | .`

## source attribution

- input: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- normalized: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- decoded: `Source public://pmc/PMC4457059 (2015-6-4) reports that the claim is tied to the described study.`
- token ids: `5984, 1462, 2452, 11264, 22515, 22571, 22540, 12299, 22563, 6073, 3991, 16398, 5751, 22560, 5165, 4177, 22547, 3967, 341, 265, 2879, 312, 10555, 294, 265, 3485, 965, 22524`
- token pieces: `[ws]Source | [ws]public | :// | pm | c | / | P | MC | 4 | 45 | 70 | 59 | [ws](201 | 5 | -6 | -4 | ) | [ws]reports | [ws]that | [ws]the | [ws]claim | [ws]is | [ws]tied | [ws]to | [ws]the | [ws]described | [ws]study | .`

## uncertainty and abstention

- input: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- normalized: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- decoded: `The available inputs do not provide enough evidence, so the shell should preserve uncertainty or abstain.`
- token ids: `331, 1756, 14548, 572, 452, 1681, 2050, 2708, 22525, 588, 265, 4887, 1061, 7858, 13866, 367, 18551, 408, 22524`
- token pieces: `[ws]The | [ws]available | [ws]inputs | [ws]do | [ws]not | [ws]provide | [ws]enough | [ws]evidence | , | [ws]so | [ws]the | [ws]shell | [ws]should | [ws]preserve | [ws]uncertainty | [ws]or | [ws]abst | ain | .`

## project verbalization

- input: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- normalized: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- decoded: `The confidence object indicates low confidence, so the shell should preserve uncertainty rather than overstate the claim.`
- token ids: `331, 8494, 1868, 6268, 1376, 8494, 22525, 588, 265, 4887, 1061, 7858, 13866, 2418, 616, 643, 6127, 265, 2879, 22524`
- token pieces: `[ws]The | [ws]confidence | [ws]object | [ws]indicates | [ws]low | [ws]confidence | , | [ws]so | [ws]the | [ws]shell | [ws]should | [ws]preserve | [ws]uncertainty | [ws]rather | [ws]than | [ws]over | state | [ws]the | [ws]claim | .`

