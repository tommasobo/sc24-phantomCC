class Option:
    def __init__(self, name, value):
        self.name = name
        self.value = value

    def is_flag(self):
        return False
    
    def cmdline(self):
        return f"{self.name} {self.value}"
    
    def readable(self):
        return f"{self.name}={self.value}"
    
    def __str__(self):
        return f"Option({self.name}, {self.value})"
    
    def short_hand(self):
        return self.readable()

    def __repr__(self):
        return self.__str__()
    
class Flag(Option):
    def __init__(self, name, value):
        super().__init__(name, value)
    
    def is_flag(self):
        return True
    
class DefaultOnFlag(Flag):
    def __init__(self, name, value):
        super().__init__(name, value)
    
    def cmdline(self):
        if self.value:
            return ""
        else:
            return self.name    
        
class DefaultOffFlag(Flag):
    def __init__(self, name, value):
        super().__init__(name, value)
    
    def cmdline(self):
        if self.value:
            return self.name
        else:
            return ""
    
class Alpha(Option):
    def __init__(self, value):
        super().__init__("-alpha", value)
    
class Beta(Option):
    def __init__(self, value):
        super().__init__("-beta", value)

class KMin(Option):
    def __init__(self, value):
        super().__init__("-kmin", value)

class KMax(Option):
    def __init__(self, value):
        super().__init__("-kmax", value)

class FastIncreaseThreshold(Option):
    def __init__(self, value):
        super().__init__("-fast-increase-threshold", value)
    def short_hand(self):
        return f"-fit={self.value}"
    
class FastIncrease(DefaultOnFlag):
    def __init__(self, value):
        super().__init__("-no-fi", value)
        
class QuickAdapt(DefaultOnFlag):
    def __init__(self, value):
        super().__init__("-no-qa", value)

class QueueSizeRatio(Option):
    def __init__(self, value):
        super().__init__("-queue_size_ratio", value)

class PacingBonus(Option):
    def __init__(self, value):
        super().__init__("-pacing-bonus", value)

class UseRegularEwma(DefaultOffFlag):
    def __init__(self, value):
        super().__init__("-use-regular-ewma", value)

class PerAck(DefaultOffFlag):
    def __init__(self, value):
        super().__init__("-per-ack", value)

class StartingCWND(Option):
    def __init__(self, value):
        super().__init__("-starting_cwnd", value)

class InterAlgo(Option):
    def __init__(self, value):
        super().__init__("-inter-algo", value)

class UseScheme2(DefaultOffFlag):
    def __init__(self, value):
        super().__init__("-use-scheme-2", value)

class ConsecutiveDecreasesForQuickAdapt(Option):
    def __init__(self, value):
        super().__init__("-consec-epochs-qa", value)
    def short_hand(self):
        return f"-ceqa={self.value}"

class BonusDrop(Option):
    def __init__(self, value):
        super().__init__("-bonus_drop", value)

class ECNAlpha(Option):
    def __init__(self, value):
        super().__init__("-ecn_alpha", value)

class OptionSet:
    def __init__(self, options):
        self.options = options
    
    def cmdline(self):
        return " ".join([option.cmdline() for option in self.options])
    
    def readable(self):
        s = "".join([option.readable() for option in self.options])
        if len(s) == s.count("-"):
            return ""
        return s
    
    def short_hand(self):
        s = "".join([option.short_hand() for option in self.options])
        if len(s) == s.count("-"):
            return ""
        return s
    
    
    def __str__(self):
        return f"OptionSet({[str(option) for option in self.options]})"
    
    def __repr__(self):
        return self.__str__()
    
def get_folder_name_from_options(options):
    return "".join([option.short_hand() for option in options])
    
def make_all_configs(config):
    if len(config) == 0:
        return [[]]
    else:
        return [[option] + rest for option in config[0] for rest in make_all_configs(config[1:])]