class ObjectFile
  attr_reader :file, :library
  def initialize(file, library)
    @file = File.basename(file)
    @library = library ? File.basename(library) : nil
  end

  def inspect
    [@file, @library]
  end
end

class Sym
  attr_reader :addr, :name, :size, :obj, :sect
  def initialize(addr, name, size, obj, sect)
    @addr = addr
    @name = name
    @size = size
    @obj = obj
    @sect = sect
  end

  def inspect
    [@addr, @name, @size, @obj]
  end
end

class Section
  attr_reader :addr, :size, :seg, :name
  def initialize(addr, size, seg, name)
    @addr = addr
    @size = size
    @seg = seg
    @name = name
  end
end

def obj_array(file, library)
  [File.basename(file), library ? File.basename(library) : nil]
end

class Linkmap
  attr_reader :objs, :syms
  def initialize(path)
    lines = open(path, 'r') { |io| io.read.encode("UTF-8", invalid: :replace, replace: "") }.split("\n")
    # Sometimes it's nice to add blank lines while playing with it to space things
    lines = lines.select { |line| line != "" }
    obj_start = lines.index('# Object files:')
    sect_start = lines.index('# Sections:')
    sym_start = lines.index('# Symbols:')
    strip_start = lines.index('# Dead Stripped Symbols:')

    @sects = ((sect_start+1)...sym_start).map do |i|
      pieces = lines[i].split(/\s+/)
      Section.new(pieces[0].to_i(16), pieces[1].to_i(16), pieces[2], pieces[3])
    end

    @objs = ((obj_start+1)...sect_start).map do |i|
      line = lines[i]
      line = line[(line.index(']')+2)..-1]
      m = line.match(/(.*)\((.*)\)$/)
      if m
        next ObjectFile.new(m[2], m[1])
      else
        next ObjectFile.new(line, nil)
      end
    end

    @syms = ((sym_start+2)...(strip_start-1)).map do |i|
      line = lines[i]
      addr_str, size_str, obj_idx_str, name = line.match(/^(\S+)\s+(\S+)\s+\[(.*?)\]\s+(.*)/)[1..-1]
      addr = addr_str.to_i(16)
      size = size_str.to_i(16)
      obj_idx = obj_idx_str.to_i
      obj = objs[obj_idx]
      sect = @sects.detect do |sect|
        sect.addr <= addr && addr < sect.addr + sect.size
      end
      binding.pry if !sect
      Sym.new(addr, name, size, obj, sect)
    end

    @addr_to_sym = @syms.map { |sym| [sym.addr, sym] }.to_h
  end

  def symbolicate(addr)
    return nil if addr < @syms.first.addr || addr > @syms.last.addr + @syms.last.size
    addr.downto(0).each do |test_addr|
      sym = @addr_to_sym[test_addr]
      return sym if sym
    end
  end
end
