def try!(allow_failure: false, show_output: true, cmd:)
  success = show_output ? system(cmd) : system(cmd, STDOUT => "/dev/null")
  raise "Command failed: #{cmd}" unless success || allow_failure
end


# It's like .detect, except that it verifies that the one element it's returning is the only one
# If no block is passed, it's like .detect(&:itself)
def detect_only(array, &block)
  matches = block ? array.select(&block) : array
  raise "Found array without just one element: #{matches.to_s}" unless matches.size == 1
  matches[0]
end

def expand_ids(obj, id_to_obj)
  if obj.is_a?(Array)
    obj.map { |elem| expand_ids(elem, id_to_obj) }
  elsif obj.is_a?(Hash)
    ref_id = obj["@ref"]
    if ref_id
      raise unless obj.size == 1
      ref_obj = id_to_obj[ref_id]
      raise unless ref_obj
      return ref_obj
    end
    id = obj["@id"]
    new_obj = obj.map { |k, v| [k, expand_ids(v, id_to_obj)] }.to_h
    id_to_obj[id] = new_obj if id
    new_obj
  else
    obj
  end
end
