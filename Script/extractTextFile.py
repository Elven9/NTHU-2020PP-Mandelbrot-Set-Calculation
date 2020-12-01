# Open Text File
# Format:
# [rank]Type: Value
# Type can be a string with multiple space between each words
# rank is a number range from 0 ~ size-1
# Value is a number assuming as a float

class OutputExtractor:
    def __init__(self, fileName:str, rankSize: int):
        # Initializae All Setting
        self.fileName = fileName
        self.rankSize = rankSize
        
        # Extract File Raw Material
        with open(self.fileName, "r") as tf:
            self.rawLines = tf.readlines()

        # Preprocessing Result
        self.data = []
        lines = list(map(lambda x: x.strip("[\n")[:-5], self.rawLines))
        lines = list(map(lambda x: x.split("]"), lines))

        for i in range(self.rankSize):
            # Get Rank Information
            self.data.append({})

            for j in range(len(lines)):
                if int(lines[j][0]) == i:
                    tmp = lines[j][1].split(":")
                    self.data[i][tmp[0]] = float(tmp[1])

        # Extract Keys
        self.types = list(self.data[0].keys())

    def extractType(self, typeName: str):
        # Extract All Ranks' Specific Type Information.
        result = []
        if typeName not in self.types: return result
        for i in range(self.rankSize):
            result.append({
                "rank": i,
                "type": typeName,
                "value": self.data[i][typeName]
            })

        return result

    def extractRank(self, rank: int):
        # Extrack Specific Rank's All Information.
        return self.data[rank]
        
    def extractMax(self, typeName: str):
        # Extrack All Information Maxima(Bottle Neck).
        result = {
            "rank": 0,
            "value": 0,
            "type": typeName
        }
        for rank in range(self.rankSize):
            if self.data[rank][typeName] > result["value"]:
                result["value"] = self.data[rank][typeName]
                result["rank"] = rank

        return result

    def extractMaxMinAll(self):
        result = []

        for t in self.types:
            tmp = {
                "type": t,
                'max': float('-inf'),
                'min': float('inf')
            }

            for rank in range(self.rankSize):
                if self.data[rank][t] > tmp["max"]:
                    tmp["max"] = self.data[rank][t]
                if self.data[rank][t] < tmp["min"]:
                    tmp["min"] = self.data[rank][t]
            result.append(tmp)
        return result

    def __str__(self):
        tmp = ""
        for i in range(len(self.data)):
            tmp += f"rank: {i}\n"

            for key in self.data[i].keys():
                tmp += f"   {key}: {self.data[i][key]}\n"
        return tmp


if __name__ == "__main__":
    fileN = input("FileName: ")
    rankSize = int(input("RankSize: "))

    test = OutputExtractor(fileN, rankSize)
    print(test)
    typeName = input("Get Type Name: ")
    targetRank = int(input("Target rank: "))
    print("Extract Type: ")
    print(test.extractType(typeName))
    print("Extract Rank: ")
    print(test.extractRank(targetRank))
    print("ExtractMax: ")
    print(test.extractMax(typeName))